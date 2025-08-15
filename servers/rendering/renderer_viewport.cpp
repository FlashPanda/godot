/**************************************************************************/
/*  renderer_viewport.cpp                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "renderer_viewport.h"

#include "core/config/project_settings.h"
#include "core/math/transform_interpolator.h"
#include "core/object/worker_thread_pool.h"
#include "renderer_canvas_cull.h"
#include "renderer_scene_cull.h"
#include "rendering_server_globals.h"
#include "storage/texture_storage.h"
#include "renderer_rd/storage_rd/render_scene_buffers_rd.h"

static Transform2D _canvas_get_transform(RendererViewport::Viewport *p_viewport, RendererCanvasCull::Canvas *p_canvas, RendererViewport::Viewport::CanvasData *p_canvas_data, const Vector2 &p_vp_size) {
	Transform2D xf = p_viewport->global_transform;

	Vector2 pixel_snap_offset;
	if (p_viewport->snap_2d_transforms_to_pixel) {
		// We use `floor(p + 0.5)` to snap canvas items, but `ceil(p - 0.5)`
		// to snap viewport transform because the viewport transform is inverse
		// to the camera transform. Also, if the viewport size is not divisible
		// by 2, the center point is offset by 0.5 px and we need to add 0.5
		// before rounding to cancel it out.
		pixel_snap_offset.x = (p_viewport->size.width % 2) ? 0.0 : -0.5;
		pixel_snap_offset.y = (p_viewport->size.height % 2) ? 0.0 : -0.5;
	}

	float scale = 1.0;
	if (p_viewport->canvas_map.has(p_canvas->parent)) {
		Transform2D c_xform = p_viewport->canvas_map[p_canvas->parent].transform;
		if (p_viewport->snap_2d_transforms_to_pixel) {
			c_xform.columns[2] = (c_xform.columns[2] * p_canvas->parent_scale + pixel_snap_offset).ceil() / p_canvas->parent_scale;
		}
		xf = xf * c_xform;
		scale = p_canvas->parent_scale;
	}

	Transform2D c_xform = p_canvas_data->transform;
	if (p_viewport->snap_2d_transforms_to_pixel) {
		c_xform.columns[2] = (c_xform.columns[2] + pixel_snap_offset).ceil();
	}
	xf = xf * c_xform;

	if (scale != 1.0 && !RSG::canvas->disable_scale) {
		Vector2 pivot = p_vp_size * 0.5;
		Transform2D xfpivot;
		xfpivot.set_origin(pivot);
		Transform2D xfscale;
		xfscale.scale(Vector2(scale, scale));

		xf = xfpivot.affine_inverse() * xf;
		xf = xfscale * xf;
		xf = xfpivot * xf;
	}

	return xf;
}

Vector<RendererViewport::Viewport *> RendererViewport::_sort_active_viewports() {
	// We need to sort the viewports in a "topological order", children first and
	// parents last. We also need to keep sibling viewports in the original order
	// from top to bottom.

	Vector<Viewport *> result;
	List<Viewport *> nodes;

	for (int i = active_viewports.size() - 1; i >= 0; --i) {
		Viewport *viewport = active_viewports[i];
		if (viewport->parent.is_valid()) {
			continue;
		}

		nodes.push_back(viewport);
		result.insert(0, viewport);
	}

	while (!nodes.is_empty()) {
		const Viewport *node = nodes.front()->get();
		nodes.pop_front();

		for (int i = active_viewports.size() - 1; i >= 0; --i) {
			Viewport *child = active_viewports[i];
			if (child->parent != node->self) {
				continue;
			}

			if (!nodes.find(child)) {
				nodes.push_back(child);
				result.insert(0, child);
			}
		}
	}

	return result;
}

void RendererViewport::_configure_3d_render_buffers(Viewport *p_viewport) {
	if (p_viewport->render_buffers.is_valid()) {
		if (p_viewport->size.width == 0 || p_viewport->size.height == 0) {
			p_viewport->render_buffers.unref();
		} else {
			const float EPSILON = 0.0001;
			float scaling_3d_scale = p_viewport->scaling_3d_scale;
			RS::ViewportScaling3DMode scaling_3d_mode = p_viewport->scaling_3d_mode;
			bool upscaler_available = p_viewport->fsr_enabled;
			RS::ViewportScaling3DType scaling_type = RS::scaling_3d_mode_type(scaling_3d_mode);

			if ((!upscaler_available || (scaling_type == RS::VIEWPORT_SCALING_3D_TYPE_SPATIAL)) && scaling_3d_scale >= (1.0 - EPSILON) && scaling_3d_scale <= (1.0 + EPSILON)) {
				// No 3D scaling for spatial modes? Ignore scaling mode, this just introduces overhead.
				// - Mobile can't perform optimal path
				// - FSR does an extra pass (or 2 extra passes if 2D-MSAA is enabled)
				// Scaling = 1.0 on FSR2 and MetalFX temporal has benefits
				scaling_3d_scale = 1.0;
				scaling_3d_mode = RS::VIEWPORT_SCALING_3D_MODE_OFF;
			}

			// Verify MetalFX upscaling support.
			if (
					(scaling_3d_mode == RS::VIEWPORT_SCALING_3D_MODE_METALFX_TEMPORAL && !RD::get_singleton()->has_feature(RD::SUPPORTS_METALFX_TEMPORAL)) ||
					(scaling_3d_mode == RS::VIEWPORT_SCALING_3D_MODE_METALFX_SPATIAL && !RD::get_singleton()->has_feature(RD::SUPPORTS_METALFX_SPATIAL))) {
				scaling_3d_mode = RS::VIEWPORT_SCALING_3D_MODE_BILINEAR;
				WARN_PRINT_ONCE("MetalFX upscaling is not supported in the current renderer. Falling back to bilinear 3D resolution scaling.");
			}

			RS::ViewportMSAA msaa_3d = p_viewport->msaa_3d;

			// If MetalFX Temporal upscaling is supported, verify limits.
			if (scaling_3d_mode == RS::VIEWPORT_SCALING_3D_MODE_METALFX_TEMPORAL) {
				double min_scale = (double)RD::get_singleton()->limit_get(RD::LIMIT_METALFX_TEMPORAL_SCALER_MIN_SCALE) / 1000'000.0;
				double max_scale = (double)RD::get_singleton()->limit_get(RD::LIMIT_METALFX_TEMPORAL_SCALER_MAX_SCALE) / 1000'000.0;
				if ((double)scaling_3d_scale < min_scale || (double)scaling_3d_scale > max_scale) {
					scaling_3d_mode = RS::VIEWPORT_SCALING_3D_MODE_BILINEAR;
					WARN_PRINT_ONCE(vformat("MetalFX temporal upscaling scale is outside limits; scale must be between %f and %f. Falling back to bilinear 3D resolution scaling.", min_scale, max_scale));
				}

				if (msaa_3d != RS::VIEWPORT_MSAA_DISABLED) {
					WARN_PRINT_ONCE("MetalFX temporal upscaling does not support 3D MSAA. Disabling 3D MSAA internally.");
					msaa_3d = RS::VIEWPORT_MSAA_DISABLED;
				}
			}

			bool scaling_3d_is_not_bilinear = scaling_3d_mode != RS::VIEWPORT_SCALING_3D_MODE_OFF && scaling_3d_mode != RS::VIEWPORT_SCALING_3D_MODE_BILINEAR;
			bool use_taa = p_viewport->use_taa;

			if (scaling_3d_is_not_bilinear && (scaling_3d_scale >= (1.0 + EPSILON))) {
				// FSR is not designed for downsampling.
				// Fall back to bilinear scaling.
				WARN_PRINT_ONCE("FSR 3D resolution scaling is not designed for downsampling. Falling back to bilinear 3D resolution scaling.");
				scaling_3d_mode = RS::VIEWPORT_SCALING_3D_MODE_BILINEAR;
			}

			if (scaling_3d_is_not_bilinear && !upscaler_available) {
				// FSR is not actually available.
				// Fall back to bilinear scaling.
				WARN_PRINT_ONCE("FSR 3D resolution scaling is not available. Falling back to bilinear 3D resolution scaling.");
				scaling_3d_mode = RS::VIEWPORT_SCALING_3D_MODE_BILINEAR;
			}

			if (use_taa && (scaling_type == RS::VIEWPORT_SCALING_3D_TYPE_TEMPORAL)) {
				// FSR2 can't be used with TAA.
				// Turn it off and prefer using FSR2.
				WARN_PRINT_ONCE("FSR 2 or MetalFX Temporal is not compatible with TAA. Disabling TAA internally.");
				use_taa = false;
			}

			int target_width;
			int target_height;
			int render_width;
			int render_height;

			switch (scaling_3d_mode) {
				case RS::VIEWPORT_SCALING_3D_MODE_BILINEAR:
					// Clamp 3D rendering resolution to reasonable values supported on most hardware.
					// This prevents freezing the engine or outright crashing on lower-end GPUs.
					target_width = p_viewport->size.width;
					target_height = p_viewport->size.height;
					render_width = CLAMP(target_width * scaling_3d_scale, 1, 16384);
					render_height = CLAMP(target_height * scaling_3d_scale, 1, 16384);
					break;
				case RS::VIEWPORT_SCALING_3D_MODE_METALFX_SPATIAL:
				case RS::VIEWPORT_SCALING_3D_MODE_METALFX_TEMPORAL:
				case RS::VIEWPORT_SCALING_3D_MODE_FSR:
				case RS::VIEWPORT_SCALING_3D_MODE_FSR2:
					target_width = p_viewport->size.width;
					target_height = p_viewport->size.height;
					render_width = MAX(target_width * scaling_3d_scale, 1.0); // target_width / (target_width * scaling)
					render_height = MAX(target_height * scaling_3d_scale, 1.0);
					break;
				case RS::VIEWPORT_SCALING_3D_MODE_OFF:
					target_width = p_viewport->size.width;
					target_height = p_viewport->size.height;
					render_width = target_width;
					render_height = target_height;
					break;
				default:
					// This is an unknown mode.
					WARN_PRINT_ONCE(vformat("Unknown scaling mode: %d. Disabling 3D resolution scaling.", scaling_3d_mode));
					scaling_3d_mode = RS::VIEWPORT_SCALING_3D_MODE_OFF;
					scaling_3d_scale = 1.0;
					target_width = p_viewport->size.width;
					target_height = p_viewport->size.height;
					render_width = target_width;
					render_height = target_height;
					break;
			}

			uint32_t jitter_phase_count = 0;
			if (scaling_type == RS::VIEWPORT_SCALING_3D_TYPE_TEMPORAL) {
				// Implementation has been copied from ffxFsr2GetJitterPhaseCount.
				// Also used for MetalFX Temporal scaling.
				jitter_phase_count = uint32_t(8.0f * pow(float(target_width) / render_width, 2.0f));
			} else if (use_taa) {
				// Default jitter count for TAA.
				jitter_phase_count = 16;
			}

			p_viewport->internal_size = Size2(render_width, render_height);
			p_viewport->jitter_phase_count = jitter_phase_count;

			// At resolution scales lower than 1.0, use negative texture mipmap bias
			// to compensate for the loss of sharpness.
			const float texture_mipmap_bias = log2f(MIN(scaling_3d_scale, 1.0)) + p_viewport->texture_mipmap_bias;

			RenderSceneBuffersConfiguration rb_config;
			rb_config.set_render_target(p_viewport->render_target);
			rb_config.set_internal_size(Size2i(render_width, render_height));
			rb_config.set_target_size(Size2(target_width, target_height));
			rb_config.set_view_count(p_viewport->view_count);
			rb_config.set_scaling_3d_mode(scaling_3d_mode);
			rb_config.set_msaa_3d(msaa_3d);
			rb_config.set_screen_space_aa(p_viewport->screen_space_aa);
			rb_config.set_fsr_sharpness(p_viewport->fsr_sharpness);
			rb_config.set_texture_mipmap_bias(texture_mipmap_bias);
			rb_config.set_anisotropic_filtering_level(p_viewport->anisotropic_filtering_level);
			rb_config.set_use_taa(use_taa);
			rb_config.set_use_debanding(p_viewport->use_debanding);

			p_viewport->render_buffers->configure(&rb_config);
		}
	}
}

void RendererViewport::_draw_3d(Viewport *p_viewport) {
#ifndef _3D_DISABLED
	RENDER_TIMESTAMP("> Render 3D Scene");

	// 获取XR接口
	Ref<XRInterface> xr_interface;
	if (p_viewport->use_xr && XRServer::get_singleton() != nullptr) {
		xr_interface = XRServer::get_singleton()->get_primary_interface();
	}

	// 如果视口使用遮挡剔除，并且遮挡缓冲已脏
	if (p_viewport->use_occlusion_culling) {
		if (p_viewport->occlusion_buffer_dirty) {
			float aspect = p_viewport->size.aspect();	// 获取视口宽高比
			int max_size = occlusion_rays_per_thread * WorkerThreadPool::get_singleton()->get_thread_count();	// 获取最大的遮挡射线数量

			// 视口大小
			int viewport_size = p_viewport->size.width * p_viewport->size.height;
			// 至少每16x16的区域有一个深度像素，最多2x2的区域有一个深度像素
			max_size = CLAMP(max_size, viewport_size / (32 * 32), viewport_size / (2 * 2)); // At least one depth pixel for every 16x16 region. At most one depth pixel for every 2x2 region.

			// 高度值获取
			float height = Math::sqrt(max_size / aspect);
			Size2i new_size = Size2i(height * aspect, height);
			// 剔除的缓存大小设置
			RendererSceneOcclusionCull::get_singleton()->buffer_set_size(p_viewport->self, new_size);
			p_viewport->occlusion_buffer_dirty = false;
		}
	}

	// 场景网格LOD阈值
	float screen_mesh_lod_threshold = p_viewport->mesh_lod_threshold / float(p_viewport->size.width);
	// 渲染相机
	RSG::scene->render_camera(p_viewport->render_buffers,
		p_viewport->camera,
		p_viewport->scenario,
		p_viewport->self,
		p_viewport->internal_size,
		p_viewport->jitter_phase_count,
		screen_mesh_lod_threshold,
		p_viewport->shadow_atlas,
		xr_interface,
		&p_viewport->render_info);
	
	RENDER_TIMESTAMP("< Render 3D Scene");
#endif // _3D_DISABLED
}

void RendererViewport::_draw_viewport(Viewport *p_viewport) {
	// 统计渲染时间，记录时间戳。
	if (p_viewport->measure_render_time) {
		String rt_id = "vp_begin_" + itos(p_viewport->self.get_id());
		RSG::utilities->capture_timestamp(rt_id);
		timestamp_vp_map[rt_id] = p_viewport->self;
	}

	// 在渲染方法是gl兼容性模式的情况下，正确设置当前要渲染的窗口
	if (OS::get_singleton()->get_current_rendering_method() == "gl_compatibility") {
		// This is currently needed for GLES to keep the current window being rendered to up to date
		DisplayServer::get_singleton()->gl_window_make_current(p_viewport->viewport_to_screen);
	}

	/* Camera should always be BEFORE any other 3D */
	// 相机需要在其他3D元素之前。

	// 立体渲染不支持2D，没有深度数据
	bool can_draw_2d = !p_viewport->disable_2d && p_viewport->view_count == 1; // Stereo rendering does not support 2D, no depth data
	// 是否将画布作为3D背景
	bool scenario_draw_canvas_bg = false; //draw canvas, or some layer of it, as BG for 3D instead of in front
	// 画布的最大层。
	int scenario_canvas_max_layer = 0;
	// 是否强制清除渲染目标
	bool force_clear_render_target = false;

	// 视口渲染信息重置
	for (int i = 0; i < RS::VIEWPORT_RENDER_INFO_TYPE_MAX; i++) {
		for (int j = 0; j < RS::VIEWPORT_RENDER_INFO_MAX; j++) {
			p_viewport->render_info.info[i][j] = 0;
		}
	}

	// 检查环境配置，确定是否将画布作为3D背景
	if (RSG::scene->is_scenario(p_viewport->scenario)) {
		// 获取环境资源ID
		RID environment = RSG::scene->scenario_get_environment(p_viewport->scenario);
		if (RSG::scene->is_environment(environment)) {
			if (can_draw_2d && !viewport_is_environment_disabled(p_viewport)) {
				scenario_draw_canvas_bg = RSG::scene->environment_get_background(environment) == RS::ENV_BG_CANVAS;
				scenario_canvas_max_layer = RSG::scene->environment_get_canvas_max_layer(environment);
			} else if (RSG::scene->environment_get_background(environment) == RS::ENV_BG_CANVAS) {
				// The scene renderer will still copy over the last frame, so we need to clear the render target.
				force_clear_render_target = true;
			}
		}
	}

	// 是否启用了3D
	bool can_draw_3d = RSG::scene->is_camera(p_viewport->camera) && !p_viewport->disable_3d;

	// 看到这里，我觉得这引擎本身是一个2D引擎，所以才需要判断是否要渲染3D，因为如果本身就是3D引擎，是不可能出现这种判断，只会判断是否有UI。
	if ((scenario_draw_canvas_bg || can_draw_3d) && !p_viewport->render_buffers.is_valid()) {
		//wants to draw 3D but there is no render buffer, create
		// 需要绘制3D但是没有渲染缓冲区的时候，就创建渲染缓冲区
		p_viewport->render_buffers = RSG::scene->render_buffers_create();

		// 根据视口配置渲染缓冲区
		_configure_3d_render_buffers(p_viewport);
	}

	// 背景颜色设置
	Color bgcolor = p_viewport->transparent_bg ? Color(0, 0, 0, 0) : RSG::texture_storage->get_default_clear_color();

	if (p_viewport->clear_mode != RS::VIEWPORT_CLEAR_NEVER) {
		RSG::texture_storage->render_target_request_clear(p_viewport->render_target, bgcolor);
		if (p_viewport->clear_mode == RS::VIEWPORT_CLEAR_ONLY_NEXT_FRAME) {
			p_viewport->clear_mode = RS::VIEWPORT_CLEAR_NEVER;	// 一帧只清除一次
		}
	}

	// 不使用画布背景，并且需要绘制3d，才绘制3d
	if (!scenario_draw_canvas_bg && can_draw_3d) {
		if (force_clear_render_target) {
			RSG::texture_storage->render_target_do_clear_request(p_viewport->render_target);
		}
		// 通过3D渲染器的路径。
		_draw_3d(p_viewport);
	}

	// Debug
	if (save_flag) {
		save_current_view(p_viewport);
		save_flag.store(false);
	}

	// 需要绘制2D
	if (can_draw_2d) {
		// 画布数据
		RBMap<Viewport::CanvasKey, Viewport::CanvasData *> canvas_map;

		// 尺寸大小
		Rect2 clip_rect(0, 0, p_viewport->size.x, p_viewport->size.y);
		// 二维光照和阴影数据
		// 画布渲染器(此渲染器是单例）
		RendererCanvasRender::Light *lights = nullptr;		
		RendererCanvasRender::Light *lights_with_shadow = nullptr;

		RendererCanvasRender::Light *directional_lights = nullptr;
		RendererCanvasRender::Light *directional_lights_with_shadow = nullptr;

		// 是否启用了sdf（有向距离场）
		if (p_viewport->sdf_active) {
			// Process SDF.
			// 获取SDF矩形大小
			Rect2 sdf_rect = RSG::texture_storage->render_target_get_sdf_rect(p_viewport->render_target);

			// 光照遮挡器实例
			RendererCanvasRender::LightOccluderInstance *occluders = nullptr;

			// Make list of occluders.
			// 遍历所有画布，收集光源和遮挡信息
			for (KeyValue<RID, Viewport::CanvasData> &E : p_viewport->canvas_map) {
				RendererCanvasCull::Canvas *canvas = static_cast<RendererCanvasCull::Canvas *>(E.value.canvas);
				// 计算当前画布到视口的变换矩阵
				Transform2D xf = _canvas_get_transform(p_viewport, canvas, &E.value, clip_rect.size);

				// 遍历该画布上的所有光遮挡器实例（用于优化光照计算的物体）
				for (RendererCanvasRender::LightOccluderInstance *F : canvas->occluders) {
					if (!F->enabled) {
						continue;
					}

					// 插值处理：当全局插值启用且该遮挡器允许插值时
					if (!RSG::canvas->_interpolation_data.interpolation_enabled || !F->interpolated) {
						F->xform_cache = xf * F->xform_curr;	// 当前帧变换乘上视口变换
					} else {
						// 获取物理插值系数（介于上一帧和当前帧之间的插值比例）
						real_t f = Engine::get_singleton()->get_physics_interpolation_fraction();
						// 执行2D变换插值计算（平滑过渡两帧间的变换状态）
						TransformInterpolator::interpolate_transform_2d(F->xform_prev, F->xform_curr, F->xform_cache, f);
						F->xform_cache = xf * F->xform_cache;
					}

					// 空间相交检测：判断遮挡器的AABB是否与SDF（有向距离场）的矩形区域相交
					if (sdf_rect.intersects_transformed(F->xform_cache, F->aabb_cache)) {
						// 将符合条件的遮挡器加入链表头部（高效插入操作）
						F->next = occluders;
						occluders = F;
					}
				}
			}

			// 渲染sdf
			RSG::canvas_render->render_sdf(p_viewport->render_target, occluders);
			// 标记渲染目标启用sdf
			RSG::texture_storage->render_target_mark_sdf_enabled(p_viewport->render_target, true);

			p_viewport->sdf_active = false; // If used, gets set active again.
		} else {
			RSG::texture_storage->render_target_mark_sdf_enabled(p_viewport->render_target, false);
		}

		// 阴影区域
		Rect2 shadow_rect;

		int shadow_count = 0;
		int directional_light_count = 0;

		RENDER_TIMESTAMP("Cull 2D Lights");
		// 2D光剔除
		for (KeyValue<RID, Viewport::CanvasData> &E : p_viewport->canvas_map) {
			RendererCanvasCull::Canvas *canvas = static_cast<RendererCanvasCull::Canvas *>(E.value.canvas);

			Transform2D xf = _canvas_get_transform(p_viewport, canvas, &E.value, clip_rect.size);

			// Find lights in canvas.
			// 找到画布中的光源

			for (RendererCanvasRender::Light *F : canvas->lights) {
				RendererCanvasRender::Light *cl = F;
				if (cl->enabled && cl->texture.is_valid()) {
					//not super efficient..
					// 获取纹理尺寸
					Size2 tsize = RSG::texture_storage->texture_size_with_proxy(cl->texture);
					tsize *= cl->scale;		// 进行一定的缩放

					Vector2 offset = tsize / 2.0;	// 纹理尺寸的一半作为偏移
					Rect2 local_rect = Rect2(-offset + cl->texture_offset, tsize);	// 局部矩形区域

					if (!RSG::canvas->_interpolation_data.interpolation_enabled || !cl->interpolated) {
						cl->xform_cache = xf * cl->xform_curr;
					} else {
						// 物理插值比例
						real_t f = Engine::get_singleton()->get_physics_interpolation_fraction();
						// 2d变换的插值
						TransformInterpolator::interpolate_transform_2d(cl->xform_prev, cl->xform_curr, cl->xform_cache, f);
						cl->xform_cache = xf * cl->xform_cache;
					}

					cl->rect_cache = cl->xform_cache.xform(local_rect);

					// 裁剪区域相交计算
					if (clip_rect.intersects(cl->rect_cache)) {
						cl->filter_next_ptr = lights;
						lights = cl;
						Transform2D scale;
						scale.scale(local_rect.size);
						scale.columns[2] = local_rect.position;
						cl->light_shader_xform = cl->xform_cache * scale;
						if (cl->use_shadow) {
							cl->shadows_next_ptr = lights_with_shadow;
							if (lights_with_shadow == nullptr) {
								shadow_rect = cl->rect_cache;
							} else {
								shadow_rect = shadow_rect.merge(cl->rect_cache);
							}
							lights_with_shadow = cl;
							cl->radius_cache = local_rect.size.length();
						}
					}
				}
			}

			// 方向光源的处理
			for (RendererCanvasRender::Light *F : canvas->directional_lights) {
				RendererCanvasRender::Light *cl = F;
				if (cl->enabled) {	// 光源是否启用
					cl->filter_next_ptr = directional_lights;
					directional_lights = cl;
					if (!RSG::canvas->_interpolation_data.interpolation_enabled || !cl->interpolated) {
						cl->xform_cache = xf * cl->xform_curr;
					} else {
						real_t f = Engine::get_singleton()->get_physics_interpolation_fraction();
						TransformInterpolator::interpolate_transform_2d(cl->xform_prev, cl->xform_curr, cl->xform_cache, f);
						cl->xform_cache = xf * cl->xform_cache;
					}
					cl->xform_cache.columns[2] = Vector2(); //translation is pointless
					if (cl->use_shadow) {
						cl->shadows_next_ptr = directional_lights_with_shadow;
						directional_lights_with_shadow = cl;
					}

					directional_light_count++;

					if (directional_light_count == RS::MAX_2D_DIRECTIONAL_LIGHTS) {
						break;
					}
				}
			}

			canvas_map[Viewport::CanvasKey(E.key, E.value.layer, E.value.sublayer)] = &E.value;
		}

		if (lights_with_shadow) {
			//update shadows if any

			RendererCanvasRender::LightOccluderInstance *occluders = nullptr;

			RENDER_TIMESTAMP("> Render PointLight2D Shadows");
			RENDER_TIMESTAMP("Cull LightOccluder2Ds");

			//make list of occluders
			// 遍历视口内的所有画布
			for (KeyValue<RID, Viewport::CanvasData> &E : p_viewport->canvas_map) {
				// 获取当前画布的实例
				RendererCanvasCull::Canvas *canvas = static_cast<RendererCanvasCull::Canvas *>(E.value.canvas);
				// 计算画布在视口中的变换矩阵
				Transform2D xf = _canvas_get_transform(p_viewport, canvas, &E.value, clip_rect.size);

				// 遍历画布的所有遮挡器
				for (RendererCanvasRender::LightOccluderInstance *F : canvas->occluders) {
					if (!F->enabled) {
						continue;
					}
					if (!RSG::canvas->_interpolation_data.interpolation_enabled || !F->interpolated) {
						F->xform_cache = xf * F->xform_curr;	// 直接使用当前帧变换
					} else {
						// 插值计算，在物理帧之间平滑过度
						real_t f = Engine::get_singleton()->get_physics_interpolation_fraction();
						TransformInterpolator::interpolate_transform_2d(F->xform_prev, F->xform_curr, F->xform_cache, f);
						F->xform_cache = xf * F->xform_cache;
					}
					// 检测遮挡器包围盒是否与阴影区域相交
					if (shadow_rect.intersects_transformed(F->xform_cache, F->aabb_cache)) {
						F->next = occluders;	// 链表头插法
						occluders = F;
					}
				}
			}
			//update the light shadowmaps with them

			// 遍历所有需要投射阴影的光源
			RendererCanvasRender::Light *light = lights_with_shadow;
			while (light) {
				RENDER_TIMESTAMP("Render PointLight2D Shadow");

				// 更新光源在画布上的阴影
				RSG::canvas_render->light_update_shadow(light->light_internal,	// 光源内部标识
					shadow_count++,		// 自增阴影计数
					light->xform_cache.affine_inverse(),	// 光源逆变换矩阵
					light->item_shadow_mask,
					light->radius_cache / 1000.0,
					light->radius_cache * 1.1,
					occluders,	// 遮挡器列表
					light->rect_cache);		// 光源影响区域
				light = light->shadows_next_ptr;	// 下一个光源
			}

			RENDER_TIMESTAMP("< Render PointLight2D Shadows");
		}

		if (directional_lights_with_shadow) {
			//update shadows if any
			// 遍历定向光源
			RendererCanvasRender::Light *light = directional_lights_with_shadow;
			while (light) {
				// 光源的照射方向
				Vector2 light_dir = -light->xform_cache.columns[1].normalized(); // Y is light direction
				// 光源的有效照射范围
				float cull_distance = light->directional_distance;

				// 计算光源方向的符号向量（用于确定阴影投射象限）
				Vector2 light_dir_sign;
				light_dir_sign.x = (ABS(light_dir.x) < CMP_EPSILON) ? 0.0 : ((light_dir.x > 0.0) ? 1.0 : -1.0);
				light_dir_sign.y = (ABS(light_dir.y) < CMP_EPSILON) ? 0.0 : ((light_dir.y > 0.0) ? 1.0 : -1.0);

				 // 预定义6个顶点数组（最大可能生成的阴影几何顶点数）
				Vector2 points[6];
				int point_count = 0;

				// 遍历视口裁剪矩形的四个角落
				for (int j = 0; j < 4; j++) {
					// 定义四个角落的标准化坐标（0,0到1,1范围）
					static const Vector2 signs[4] = { Vector2(1, 1), Vector2(1, 0), Vector2(0, 0), Vector2(0, 1) };
					// 转换为-1到1范围的坐标系
					Vector2 sign_cmp = signs[j] * 2.0 - Vector2(1.0, 1.0);
					// 转换为实际屏幕坐标
					Vector2 point = clip_rect.position + clip_rect.size * signs[j];

					// 判断顶点与光源方向的关系
					if (sign_cmp == light_dir_sign) {
						//both point in same direction, plot offsetted
						// 相同方向象限：顶点沿光源方向偏移剔除距离
						points[point_count++] = point + light_dir * cull_distance;
					} else if (sign_cmp.x == light_dir_sign.x || sign_cmp.y == light_dir_sign.y) {
						// 部分方向一致：生成边缘线段
						int next_j = (j + 1) % 4;
						Vector2 next_sign_cmp = signs[next_j] * 2.0 - Vector2(1.0, 1.0);

						//one point in the same direction, plot segment

						if (next_sign_cmp.x == light_dir_sign.x || next_sign_cmp.y == light_dir_sign.y) {
							// 连续两个顶点在同一方向象限
							if (light_dir_sign.x != 0.0 || light_dir_sign.y != 0.0) {
								points[point_count++] = point;// 添加原始顶点
							}
							points[point_count++] = point + light_dir * cull_distance;	// 添加偏移顶点
						} else {
							// 单个顶点在方向象限
							points[point_count++] = point + light_dir * cull_distance;
							if (light_dir_sign.x != 0.0 || light_dir_sign.y != 0.0) {
								points[point_count++] = point;
							}
						}
					} else {
						//plot normally
						// 其他情况保留原始顶点
						points[point_count++] = point;
					}
				}

				// 存储变换后的顶点坐标
				Vector2 xf_points[6];

				RendererCanvasRender::LightOccluderInstance *occluders = nullptr;

				RENDER_TIMESTAMP("> Render DirectionalLight2D Shadows");

				// Make list of occluders.
				// 收集所有可见遮挡器（与点光源处理类似但包含方向特性）
				for (KeyValue<RID, Viewport::CanvasData> &E : p_viewport->canvas_map) {
					RendererCanvasCull::Canvas *canvas = static_cast<RendererCanvasCull::Canvas *>(E.value.canvas);
					// 获取画布空间变换矩阵
					Transform2D xf = _canvas_get_transform(p_viewport, canvas, &E.value, clip_rect.size);

					for (RendererCanvasRender::LightOccluderInstance *F : canvas->occluders) {
						if (!F->enabled) {
							continue;
						}
						// 计算遮挡器的插值变换（与物理帧同步）
						if (!RSG::canvas->_interpolation_data.interpolation_enabled || !F->interpolated) {
							F->xform_cache = xf * F->xform_curr;
						} else {
							real_t f = Engine::get_singleton()->get_physics_interpolation_fraction();
							TransformInterpolator::interpolate_transform_2d(F->xform_prev, F->xform_curr, F->xform_cache, f);
							F->xform_cache = xf * F->xform_cache;
						}
						// 计算逆变换用于空间坐标转换
						Transform2D localizer = F->xform_cache.affine_inverse();

						// 将屏幕空间顶点转换到遮挡器本地空间
						for (int j = 0; j < point_count; j++) {
							xf_points[j] = localizer.xform(points[j]);
						}
						// 检测遮挡器包围盒是否与阴影区域相交
						if (F->aabb_cache.intersects_filled_polygon(xf_points, point_count)) {
							// 链表方式收集有效遮挡器
							F->next = occluders;
							occluders = F;
						}
					}
				}

				// 调用底层渲染器更新方向光源阴影（核心渲染指令）
				RSG::canvas_render->light_update_directional_shadow(
					light->light_internal,		// 光源内部标识
					shadow_count++,				// 自增阴影计数器
					light->xform_cache,			// 光源变换矩阵
					light->item_shadow_mask,	// 阴影遮罩位
					cull_distance,				// 有效照射距离
					clip_rect,					// 当前视口裁剪区域
					occluders);					// 收集的遮挡器链表

				light = light->shadows_next_ptr;	// 遍历下一个光源
			}

			RENDER_TIMESTAMP("< Render DirectionalLight2D Shadows");
		}

		if (scenario_draw_canvas_bg && canvas_map.begin() && canvas_map.begin()->key.get_layer() > scenario_canvas_max_layer) {
			// There may be an outstanding clear request if a clear was requested, but no 2D elements were drawn.
			// Clear now otherwise we copy over garbage from the render target.
			// 如果存在未完成的清除请求（例如请求了清除但未绘制任何2D元素）
			// 此时强制清除渲染目标的残留数据，避免复制到无效数据
			RSG::texture_storage->render_target_do_clear_request(p_viewport->render_target);
			if (!can_draw_3d) {
				// 若禁用3D渲染，则渲染一个空场景（仅保留光照/阴影等基础信息）
				RSG::scene->render_empty_scene(p_viewport->render_buffers, p_viewport->scenario, p_viewport->shadow_atlas);
			} else {
				// 启用3D渲染时，执行完整的3D场景绘制流程
				_draw_3d(p_viewport);
			}
			// 标记画布背景绘制完成，避免重复处理
			scenario_draw_canvas_bg = false;
		}

		for (const KeyValue<Viewport::CanvasKey, Viewport::CanvasData *> &E : canvas_map) {
			RendererCanvasCull::Canvas *canvas = static_cast<RendererCanvasCull::Canvas *>(E.value->canvas);

			Transform2D xform = _canvas_get_transform(p_viewport, canvas, E.value, clip_rect.size);

			RendererCanvasRender::Light *canvas_lights = nullptr;
			RendererCanvasRender::Light *canvas_directional_lights = nullptr;

			// 筛选光源
			RendererCanvasRender::Light *ptr = lights;
			while (ptr) {
				if (E.value->layer >= ptr->layer_min && E.value->layer <= ptr->layer_max) {
					ptr->next_ptr = canvas_lights;
					canvas_lights = ptr;
				}
				ptr = ptr->filter_next_ptr;
			}

			ptr = directional_lights;
			while (ptr) {
				if (E.value->layer >= ptr->layer_min && E.value->layer <= ptr->layer_max) {
					ptr->next_ptr = canvas_directional_lights;
					canvas_directional_lights = ptr;
				}
				ptr = ptr->filter_next_ptr;
			}

			// 绘制画布
			RSG::canvas->render_canvas(
				p_viewport->render_target,
				canvas,
				xform,
				canvas_lights,
				canvas_directional_lights,
				clip_rect,
				p_viewport->texture_filter,
				p_viewport->texture_repeat,
				p_viewport->snap_2d_transforms_to_pixel,
				p_viewport->snap_2d_vertices_to_pixel,
				p_viewport->canvas_cull_mask,
				&p_viewport->render_info);
			if (RSG::canvas->was_sdf_used()) {
				p_viewport->sdf_active = true;
			}

			// 画布层级超过最大预设值，优先渲染3D场景。
			if (scenario_draw_canvas_bg && E.key.get_layer() >= scenario_canvas_max_layer) {
				// There may be an outstanding clear request if a clear was requested, but no 2D elements were drawn.
				// Clear now otherwise we copy over garbage from the render target.
				RSG::texture_storage->render_target_do_clear_request(p_viewport->render_target);
				if (!can_draw_3d) {
					RSG::scene->render_empty_scene(p_viewport->render_buffers, p_viewport->scenario, p_viewport->shadow_atlas);
				} else {
					_draw_3d(p_viewport);
				}

				scenario_draw_canvas_bg = false;
			}
		}

		// 如果到这一步还没有绘制的话，就绘制3D场景
		if (scenario_draw_canvas_bg) {
			// There may be an outstanding clear request if a clear was requested, but no 2D elements were drawn.
			// Clear now otherwise we copy over garbage from the render target.
			RSG::texture_storage->render_target_do_clear_request(p_viewport->render_target);
			if (!can_draw_3d) {
				RSG::scene->render_empty_scene(p_viewport->render_buffers, p_viewport->scenario, p_viewport->shadow_atlas);
			} else {
				_draw_3d(p_viewport);
			}
		}
	}

	if (RSG::texture_storage->render_target_is_clear_requested(p_viewport->render_target)) {
		//was never cleared in the end, force clear it
		// 还需要清空的话就强制清空
		RSG::texture_storage->render_target_do_clear_request(p_viewport->render_target);
	}

	// 是否需要2D的msaa
	if (RSG::texture_storage->render_target_get_msaa_needs_resolve(p_viewport->render_target)) {
		WARN_PRINT_ONCE("2D MSAA is enabled while there is no 2D content. Disable 2D MSAA for better performance.");
		RSG::texture_storage->render_target_do_msaa_resolve(p_viewport->render_target);
	}

	// 渲染时间测量
	if (p_viewport->measure_render_time) {
		String rt_id = "vp_end_" + itos(p_viewport->self.get_id());
		RSG::utilities->capture_timestamp(rt_id);
		timestamp_vp_map[rt_id] = p_viewport->self;
	}
}

void RendererViewport::draw_viewports(bool p_swap_buffers) {
	timestamp_vp_map.clear();

#ifndef _3D_DISABLED
	// get our xr interface in case we need it
	// 获取 XR 接口（若存在），供后续使用。
	Ref<XRInterface> xr_interface;
	XRServer *xr_server = XRServer::get_singleton();
	if (xr_server != nullptr) {
		// let our XR server know we're about to render our frames so we can get our frame timing
		// 通知 XR 服务器即将进行渲染，以便获得帧同步相关的信息（如帧开始时间点）
		xr_server->pre_render();

		// retrieve the interface responsible for rendering
		// 获取主要用来做渲染的 XRInterface（若存在）。
		xr_interface = xr_server->get_primary_interface();
	}
#endif // _3D_DISABLED

	// 如果在编辑器环境下，则设置默认的清除颜色（clear color）。
	if (Engine::get_singleton()->is_editor_hint()) {
		RSG::texture_storage->set_default_clear_color(GLOBAL_GET("rendering/environment/defaults/default_clear_color"));
	}

	// 如果需要重新排序活跃的 viewports（因为它们的顺序可能被改变了），就重新排序，并将标记置为 false。
	if (sorted_active_viewports_dirty) {
		sorted_active_viewports = _sort_active_viewports();
		sorted_active_viewports_dirty = false;
	}

	// 用于记录最终要 blit 到屏幕的参数列表，按 WindowID 分开存储。
	HashMap<DisplayServer::WindowID, Vector<BlitToScreen>> blit_to_screen_list;
	//draw viewports
	RENDER_TIMESTAMP("> Render Viewports");

	//determine what is visible
	draw_viewports_pass++;

	// 要确定父节点的可见性，需要从后往前遍历
	for (int i = sorted_active_viewports.size() - 1; i >= 0; i--) { //to compute parent dependency, must go in reverse draw order

		Viewport *vp = sorted_active_viewports[i];

		// 这个视口不要更新，则跳过
		if (vp->update_mode == RS::VIEWPORT_UPDATE_DISABLED) {
			continue;
		}

		if (!vp->render_target.is_valid()) {
			continue;
		}
		//ERR_CONTINUE(!vp->render_target.is_valid());

		// 视口到屏幕空间是否不为空
		bool visible = vp->viewport_to_screen_rect != Rect2();

#ifndef _3D_DISABLED
		if (vp->use_xr) {
			if (xr_interface.is_valid()) {
				// Ignore update mode we have to commit frames to our XR interface
				visible = true;

				// Override our size, make sure it matches our required size and is created as a stereo target
				Size2 xr_size = xr_interface->get_render_target_size();
				_viewport_set_size(vp, xr_size.width, xr_size.height, xr_interface->get_view_count());
			} else {
				// don't render anything
				visible = false;
				vp->size = Size2();
			}
		} else
#endif // _3D_DISABLED
		{	// 判断是否可见
			
			if (vp->update_mode == RS::VIEWPORT_UPDATE_ALWAYS || vp->update_mode == RS::VIEWPORT_UPDATE_ONCE) {
				visible = true;
			}

			if (vp->update_mode == RS::VIEWPORT_UPDATE_WHEN_VISIBLE && RSG::texture_storage->render_target_was_used(vp->render_target)) {
				visible = true;
			}

			if (vp->update_mode == RS::VIEWPORT_UPDATE_WHEN_PARENT_VISIBLE) {
				Viewport *parent = viewport_owner.get_or_null(vp->parent);
				if (parent && parent->last_pass == draw_viewports_pass) {
					visible = true;
				}
			}
		}

		visible = visible && vp->size.x > 1 && vp->size.y > 1;

		if (visible) {
			// 如果可见，则记录一下它在本次渲染循环中被使用过
			vp->last_pass = draw_viewports_pass;
		}
	}

	int vertices_drawn = 0;
	int objects_drawn = 0;
	int draw_calls_used = 0;

	// Test
	//static int count = 0;
	//++count;

	// 正向遍历视口，执行真正的渲染。
	for (int i = 0; i < sorted_active_viewports.size(); i++) {
		Viewport *vp = sorted_active_viewports[i];

		// 为什么这里会用这种判断跳过？
		if (vp->last_pass != draw_viewports_pass) {
			continue; //should not draw
		}

		RENDER_TIMESTAMP("> Render Viewport " + itos(i));

		RSG::texture_storage->render_target_set_as_unused(vp->render_target);
#ifndef _3D_DISABLED
		if (vp->use_xr && xr_interface.is_valid()) {
			// Inform XR interface we're about to render its viewport,
			// if this returns false we don't render.
			// This usually is a result of the player taking off their headset and OpenXR telling us to skip
			// rendering frames.
			if (xr_interface->pre_draw_viewport(vp->render_target)) {
				RSG::texture_storage->render_target_set_override(vp->render_target,
						xr_interface->get_color_texture(),
						xr_interface->get_depth_texture(),
						xr_interface->get_velocity_texture(),
						xr_interface->get_velocity_depth_texture());

				RSG::texture_storage->render_target_set_velocity_target_size(vp->render_target, xr_interface->get_velocity_target_size());

				if (xr_interface->get_velocity_texture().is_valid()) {
					viewport_set_force_motion_vectors(vp->self, true);
				} else {
					viewport_set_force_motion_vectors(vp->self, false);
				}

				// render...
				RSG::scene->set_debug_draw_mode(vp->debug_draw);

				// and draw viewport
				_draw_viewport(vp);

				// commit our eyes
				Vector<BlitToScreen> blits = xr_interface->post_draw_viewport(vp->render_target, vp->viewport_to_screen_rect);
				if (vp->viewport_to_screen != DisplayServer::INVALID_WINDOW_ID) {
					if (OS::get_singleton()->get_current_rendering_driver_name().begins_with("opengl3")) {
						if (blits.size() > 0) {
							RSG::rasterizer->blit_render_targets_to_screen(vp->viewport_to_screen, blits.ptr(), blits.size());
							RSG::rasterizer->gl_end_frame(p_swap_buffers);
						}
					} else if (blits.size() > 0) {
						if (!blit_to_screen_list.has(vp->viewport_to_screen)) {
							blit_to_screen_list[vp->viewport_to_screen] = Vector<BlitToScreen>();
						}

						for (int b = 0; b < blits.size(); b++) {
							blit_to_screen_list[vp->viewport_to_screen].push_back(blits[b]);
						}
					}
				}
			}
		} else
#endif // _3D_DISABLED
		{
			// 设置调试绘制模式
			RSG::scene->set_debug_draw_mode(vp->debug_draw);

			// render standard mono camera
			// 进行视口绘制
			/**********通往渲染器的路径***********/ 
			_draw_viewport(vp);

			// Test
			// 我把每个viewport都输出看看里面到底是啥妖魔鬼怪。
			// 结果是只有i=1，和i=8的时候有输出。i=1的时候是3D编辑器视口，i=8的时候整个编辑器的样子。
			//if (count == 120) {
			//	String str = vformat("user://render_target%d.png", i);
			//	RSG::rasterizer->output_rendertarget_to_image(vp->render_target, str);

			//}

			if (vp->viewport_to_screen != DisplayServer::INVALID_WINDOW_ID && (!vp->viewport_render_direct_to_screen || !RSG::rasterizer->is_low_end())) {
				//copy to screen if set as such
				// 看看有没有设置拷贝到屏幕，靠到屏幕的矩形区域来判断
				BlitToScreen blit;
				blit.render_target = vp->render_target;
				if (vp->viewport_to_screen_rect != Rect2()) {
					blit.dst_rect = vp->viewport_to_screen_rect;
				} else {
					blit.dst_rect.position = Vector2();
					blit.dst_rect.size = vp->size;
				}

				// 获取要渲染到哪些区域
				Vector<BlitToScreen> *blits = blit_to_screen_list.getptr(vp->viewport_to_screen);
				if (blits == nullptr) {
					blits = &blit_to_screen_list.insert(vp->viewport_to_screen, Vector<BlitToScreen>())->value;
				}

				// opengl3有不同的绘制路径
				if (OS::get_singleton()->get_current_rendering_driver_name().begins_with("opengl3")) {
					Vector<BlitToScreen> blit_to_screen_vec;
					blit_to_screen_vec.push_back(blit);
					RSG::rasterizer->blit_render_targets_to_screen(vp->viewport_to_screen, blit_to_screen_vec.ptr(), 1);
					RSG::rasterizer->gl_end_frame(p_swap_buffers);
				} else {
					blits->push_back(blit);
				}
			}
		}

		if (vp->update_mode == RS::VIEWPORT_UPDATE_ONCE) {
			vp->update_mode = RS::VIEWPORT_UPDATE_DISABLED;
		}

		RENDER_TIMESTAMP("< Render Viewport " + itos(i));

		// 3D render info.
		// 绘制的物体数量
		objects_drawn += vp->render_info.info[RS::VIEWPORT_RENDER_INFO_TYPE_VISIBLE][RS::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME] + vp->render_info.info[RS::VIEWPORT_RENDER_INFO_TYPE_SHADOW][RS::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME];
		// 绘制的顶点数量
		vertices_drawn += vp->render_info.info[RS::VIEWPORT_RENDER_INFO_TYPE_VISIBLE][RS::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] + vp->render_info.info[RS::VIEWPORT_RENDER_INFO_TYPE_SHADOW][RS::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME];
		// 使用的drawcall数
		draw_calls_used += vp->render_info.info[RS::VIEWPORT_RENDER_INFO_TYPE_VISIBLE][RS::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME] + vp->render_info.info[RS::VIEWPORT_RENDER_INFO_TYPE_SHADOW][RS::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME];
		// 2D render info.
		// 2D物体的绘制信息，和3D的统计到一起
		objects_drawn += vp->render_info.info[RS::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RS::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME];
		vertices_drawn += vp->render_info.info[RS::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RS::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME];
		draw_calls_used += vp->render_info.info[RS::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RS::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME];
	}
	RSG::scene->set_debug_draw_mode(RS::VIEWPORT_DEBUG_DRAW_DISABLED);

	total_objects_drawn = objects_drawn;
	total_vertices_drawn = vertices_drawn;
	total_draw_calls_used = draw_calls_used;

	RENDER_TIMESTAMP("< Render Viewports");

	// 统一绘制到屏幕上
	if (p_swap_buffers && !blit_to_screen_list.is_empty()) {
		for (const KeyValue<int, Vector<BlitToScreen>> &E : blit_to_screen_list) {
			RSG::rasterizer->blit_render_targets_to_screen(E.key, E.value.ptr(), E.value.size());
		}
	}
}

RID RendererViewport::viewport_allocate() {
	// 在分配RID的时候就已经把相关的空间分配好了。
	return viewport_owner.allocate_rid();
}

void RendererViewport::viewport_initialize(RID p_rid) {
	viewport_owner.initialize_rid(p_rid);
	Viewport *viewport = viewport_owner.get_or_null(p_rid);
	viewport->self = p_rid;
	viewport->render_target = RSG::texture_storage->render_target_create();
	viewport->shadow_atlas = RSG::light_storage->shadow_atlas_create();
	viewport->viewport_render_direct_to_screen = false;

	viewport->fsr_enabled = !RSG::rasterizer->is_low_end() && !viewport->disable_3d;
}

void RendererViewport::viewport_set_use_xr(RID p_viewport, bool p_use_xr) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->use_xr == p_use_xr) {
		return;
	}

	viewport->use_xr = p_use_xr;

	// Re-configure the 3D render buffers when disabling XR. They'll get
	// re-configured when enabling XR in draw_viewports().
	if (!p_use_xr) {
		viewport->view_count = 1;
		_configure_3d_render_buffers(viewport);
	}
}

void RendererViewport::viewport_set_scaling_3d_mode(RID p_viewport, RS::ViewportScaling3DMode p_mode) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	ERR_FAIL_COND_EDMSG(p_mode == RS::VIEWPORT_SCALING_3D_MODE_FSR && OS::get_singleton()->get_current_rendering_method() != "forward_plus", "FSR1 is only available when using the Forward+ renderer.");
	ERR_FAIL_COND_EDMSG(p_mode == RS::VIEWPORT_SCALING_3D_MODE_FSR2 && OS::get_singleton()->get_current_rendering_method() != "forward_plus", "FSR2 is only available when using the Forward+ renderer.");

	if (viewport->scaling_3d_mode == p_mode) {
		return;
	}

	bool motion_vectors_before = _viewport_requires_motion_vectors(viewport);
	viewport->scaling_3d_mode = p_mode;

	bool motion_vectors_after = _viewport_requires_motion_vectors(viewport);
	if (motion_vectors_before != motion_vectors_after) {
		num_viewports_with_motion_vectors += motion_vectors_after ? 1 : -1;
	}

	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_fsr_sharpness(RID p_viewport, float p_sharpness) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->fsr_sharpness = p_sharpness;
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_texture_mipmap_bias(RID p_viewport, float p_mipmap_bias) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->texture_mipmap_bias = p_mipmap_bias;
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_anisotropic_filtering_level(RID p_viewport, RS::ViewportAnisotropicFiltering p_anisotropic_filtering_level) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->anisotropic_filtering_level = p_anisotropic_filtering_level;
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_scaling_3d_scale(RID p_viewport, float p_scaling_3d_scale) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	// Clamp to reasonable values that are actually useful.
	// Values above 2.0 don't serve a practical purpose since the viewport
	// isn't displayed with mipmaps.
	if (viewport->scaling_3d_scale == CLAMP(p_scaling_3d_scale, 0.1, 2.0)) {
		return;
	}

	viewport->scaling_3d_scale = CLAMP(p_scaling_3d_scale, 0.1, 2.0);
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_size(RID p_viewport, int p_width, int p_height) {
	ERR_FAIL_COND(p_width < 0 || p_height < 0);

	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	ERR_FAIL_COND_MSG(viewport->use_xr, "Cannot set viewport size when using XR");

	_viewport_set_size(viewport, p_width, p_height, 1);
}

void RendererViewport::_viewport_set_size(Viewport *p_viewport, int p_width, int p_height, uint32_t p_view_count) {
	Size2i new_size(p_width, p_height);
	if (p_viewport->size != new_size || p_viewport->view_count != p_view_count) {
		p_viewport->size = new_size;
		p_viewport->view_count = p_view_count;

		RSG::texture_storage->render_target_set_size(p_viewport->render_target, p_width, p_height, p_view_count);
		_configure_3d_render_buffers(p_viewport);

		p_viewport->occlusion_buffer_dirty = true;
	}
}

bool RendererViewport::_viewport_requires_motion_vectors(Viewport *p_viewport) {
	return p_viewport->use_taa ||
			RS::scaling_3d_mode_type(p_viewport->scaling_3d_mode) == RS::VIEWPORT_SCALING_3D_TYPE_TEMPORAL ||
			p_viewport->debug_draw == RenderingServer::VIEWPORT_DEBUG_DRAW_MOTION_VECTORS || p_viewport->force_motion_vectors;
}

void RendererViewport::viewport_set_active(RID p_viewport, bool p_active) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (p_active) {
		ERR_FAIL_COND_MSG(active_viewports.has(viewport), "Can't make active a Viewport that is already active.");
		viewport->occlusion_buffer_dirty = true;
		active_viewports.push_back(viewport);
	} else {
		active_viewports.erase(viewport);
	}

	sorted_active_viewports_dirty = true;
}

void RendererViewport::viewport_set_parent_viewport(RID p_viewport, RID p_parent_viewport) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->parent = p_parent_viewport;
}

void RendererViewport::viewport_set_clear_mode(RID p_viewport, RS::ViewportClearMode p_clear_mode) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->clear_mode = p_clear_mode;
}

void RendererViewport::viewport_attach_to_screen(RID p_viewport, const Rect2 &p_rect, DisplayServer::WindowID p_screen) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (p_screen != DisplayServer::INVALID_WINDOW_ID) {
		// If using OpenGL we can optimize this operation by rendering directly to system_fbo
		// instead of rendering to fbo and copying to system_fbo after
		if (RSG::rasterizer->is_low_end() && viewport->viewport_render_direct_to_screen) {
			RSG::texture_storage->render_target_set_size(viewport->render_target, p_rect.size.x, p_rect.size.y, viewport->view_count);
			RSG::texture_storage->render_target_set_position(viewport->render_target, p_rect.position.x, p_rect.position.y);
		}

		viewport->viewport_to_screen_rect = p_rect;
		viewport->viewport_to_screen = p_screen;
	} else {
		// if render_direct_to_screen was used, reset size and position
		if (RSG::rasterizer->is_low_end() && viewport->viewport_render_direct_to_screen) {
			RSG::texture_storage->render_target_set_position(viewport->render_target, 0, 0);
			RSG::texture_storage->render_target_set_size(viewport->render_target, viewport->size.x, viewport->size.y, viewport->view_count);
		}

		viewport->viewport_to_screen_rect = Rect2();
		viewport->viewport_to_screen = DisplayServer::INVALID_WINDOW_ID;
	}
}

void RendererViewport::viewport_set_render_direct_to_screen(RID p_viewport, bool p_enable) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (p_enable == viewport->viewport_render_direct_to_screen) {
		return;
	}

	// if disabled, reset render_target size and position
	if (!p_enable) {
		RSG::texture_storage->render_target_set_position(viewport->render_target, 0, 0);
		RSG::texture_storage->render_target_set_size(viewport->render_target, viewport->size.x, viewport->size.y, viewport->view_count);
	}

	RSG::texture_storage->render_target_set_direct_to_screen(viewport->render_target, p_enable);
	viewport->viewport_render_direct_to_screen = p_enable;

	// if attached to screen already, setup screen size and position, this needs to happen after setting flag to avoid an unnecessary buffer allocation
	if (RSG::rasterizer->is_low_end() && viewport->viewport_to_screen_rect != Rect2() && p_enable) {
		RSG::texture_storage->render_target_set_size(viewport->render_target, viewport->viewport_to_screen_rect.size.x, viewport->viewport_to_screen_rect.size.y, viewport->view_count);
		RSG::texture_storage->render_target_set_position(viewport->render_target, viewport->viewport_to_screen_rect.position.x, viewport->viewport_to_screen_rect.position.y);
	}
}

void RendererViewport::viewport_set_update_mode(RID p_viewport, RS::ViewportUpdateMode p_mode) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->update_mode = p_mode;
}

RS::ViewportUpdateMode RendererViewport::viewport_get_update_mode(RID p_viewport) const {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, RS::VIEWPORT_UPDATE_DISABLED);

	return viewport->update_mode;
}

RID RendererViewport::viewport_get_render_target(RID p_viewport) const {
	const Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, RID());

	return viewport->render_target;
}

RID RendererViewport::viewport_get_texture(RID p_viewport) const {
	const Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, RID());

	return RSG::texture_storage->render_target_get_texture(viewport->render_target);
}

RID RendererViewport::viewport_get_occluder_debug_texture(RID p_viewport) const {
	const Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, RID());

	if (viewport->use_occlusion_culling && viewport->debug_draw == RenderingServer::VIEWPORT_DEBUG_DRAW_OCCLUDERS) {
		return RendererSceneOcclusionCull::get_singleton()->buffer_get_debug_texture(p_viewport);
	}
	return RID();
}

void RendererViewport::viewport_set_prev_camera_data(RID p_viewport, const RendererSceneRender::CameraData *p_camera_data) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	uint64_t frame = RSG::rasterizer->get_frame_number();
	if (viewport->prev_camera_data_frame != frame) {
		viewport->prev_camera_data = *p_camera_data;
		viewport->prev_camera_data_frame = frame;
	}
}

const RendererSceneRender::CameraData *RendererViewport::viewport_get_prev_camera_data(RID p_viewport) {
	const Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, nullptr);
	return &viewport->prev_camera_data;
}

void RendererViewport::viewport_set_disable_2d(RID p_viewport, bool p_disable) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->disable_2d = p_disable;
}

void RendererViewport::viewport_set_environment_mode(RID p_viewport, RS::ViewportEnvironmentMode p_mode) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->disable_environment = p_mode;
}

bool RendererViewport::viewport_is_environment_disabled(Viewport *viewport) {
	ERR_FAIL_NULL_V(viewport, false);

	if (viewport->parent.is_valid() && viewport->disable_environment == RS::VIEWPORT_ENVIRONMENT_INHERIT) {
		Viewport *parent = viewport_owner.get_or_null(viewport->parent);
		return viewport_is_environment_disabled(parent);
	}
	return viewport->disable_environment == RS::VIEWPORT_ENVIRONMENT_DISABLED;
}

void RendererViewport::viewport_set_disable_3d(RID p_viewport, bool p_disable) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->disable_3d = p_disable;
}

void RendererViewport::viewport_attach_camera(RID p_viewport, RID p_camera) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->camera = p_camera;
}

void RendererViewport::viewport_set_scenario(RID p_viewport, RID p_scenario) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->scenario.is_valid()) {
		RSG::scene->scenario_remove_viewport_visibility_mask(viewport->scenario, p_viewport);
	}

	viewport->scenario = p_scenario;
	if (viewport->use_occlusion_culling) {
		RendererSceneOcclusionCull::get_singleton()->buffer_set_scenario(p_viewport, p_scenario);
	}
}

void RendererViewport::viewport_attach_canvas(RID p_viewport, RID p_canvas) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	ERR_FAIL_COND(viewport->canvas_map.has(p_canvas));
	RendererCanvasCull::Canvas *canvas = RSG::canvas->canvas_owner.get_or_null(p_canvas);
	ERR_FAIL_NULL(canvas);

	canvas->viewports.insert(p_viewport);
	viewport->canvas_map[p_canvas] = Viewport::CanvasData();
	viewport->canvas_map[p_canvas].layer = 0;
	viewport->canvas_map[p_canvas].sublayer = 0;
	viewport->canvas_map[p_canvas].canvas = canvas;
}

void RendererViewport::viewport_remove_canvas(RID p_viewport, RID p_canvas) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	RendererCanvasCull::Canvas *canvas = RSG::canvas->canvas_owner.get_or_null(p_canvas);
	ERR_FAIL_NULL(canvas);

	viewport->canvas_map.erase(p_canvas);
	canvas->viewports.erase(p_viewport);
}

void RendererViewport::viewport_set_canvas_transform(RID p_viewport, RID p_canvas, const Transform2D &p_offset) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	ERR_FAIL_COND(!viewport->canvas_map.has(p_canvas));
	viewport->canvas_map[p_canvas].transform = p_offset;
}

void RendererViewport::viewport_set_transparent_background(RID p_viewport, bool p_enabled) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	if (viewport->transparent_bg == p_enabled) {
		return;
	}

	RSG::texture_storage->render_target_set_transparent(viewport->render_target, p_enabled);
	viewport->transparent_bg = p_enabled;
}

void RendererViewport::viewport_set_global_canvas_transform(RID p_viewport, const Transform2D &p_transform) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->global_transform = p_transform;
}

void RendererViewport::viewport_set_canvas_stacking(RID p_viewport, RID p_canvas, int p_layer, int p_sublayer) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	ERR_FAIL_COND(!viewport->canvas_map.has(p_canvas));
	viewport->canvas_map[p_canvas].layer = p_layer;
	viewport->canvas_map[p_canvas].sublayer = p_sublayer;
}

void RendererViewport::viewport_set_positional_shadow_atlas_size(RID p_viewport, int p_size, bool p_16_bits) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->shadow_atlas_size = p_size;
	viewport->shadow_atlas_16_bits = p_16_bits;

	RSG::light_storage->shadow_atlas_set_size(viewport->shadow_atlas, viewport->shadow_atlas_size, viewport->shadow_atlas_16_bits);
}

void RendererViewport::viewport_set_positional_shadow_atlas_quadrant_subdivision(RID p_viewport, int p_quadrant, int p_subdiv) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	RSG::light_storage->shadow_atlas_set_quadrant_subdivision(viewport->shadow_atlas, p_quadrant, p_subdiv);
}

void RendererViewport::viewport_set_msaa_2d(RID p_viewport, RS::ViewportMSAA p_msaa) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->msaa_2d == p_msaa) {
		return;
	}
	viewport->msaa_2d = p_msaa;
	RSG::texture_storage->render_target_set_msaa(viewport->render_target, p_msaa);
}

void RendererViewport::viewport_set_msaa_3d(RID p_viewport, RS::ViewportMSAA p_msaa) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->msaa_3d == p_msaa) {
		return;
	}
	viewport->msaa_3d = p_msaa;
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_use_hdr_2d(RID p_viewport, bool p_use_hdr_2d) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->use_hdr_2d == p_use_hdr_2d) {
		return;
	}
	viewport->use_hdr_2d = p_use_hdr_2d;
	RSG::texture_storage->render_target_set_use_hdr(viewport->render_target, p_use_hdr_2d);
}

bool RendererViewport::viewport_is_using_hdr_2d(RID p_viewport) const {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, false);

	return viewport->use_hdr_2d;
}

void RendererViewport::viewport_set_screen_space_aa(RID p_viewport, RS::ViewportScreenSpaceAA p_mode) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->screen_space_aa == p_mode) {
		return;
	}
	viewport->screen_space_aa = p_mode;
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_use_taa(RID p_viewport, bool p_use_taa) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	ERR_FAIL_COND_EDMSG(OS::get_singleton()->get_current_rendering_method() != "forward_plus", "TAA is only available when using the Forward+ renderer.");

	if (viewport->use_taa == p_use_taa) {
		return;
	}

	bool motion_vectors_before = _viewport_requires_motion_vectors(viewport);
	viewport->use_taa = p_use_taa;

	bool motion_vectors_after = _viewport_requires_motion_vectors(viewport);
	if (motion_vectors_before != motion_vectors_after) {
		num_viewports_with_motion_vectors += motion_vectors_after ? 1 : -1;
	}

	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_use_debanding(RID p_viewport, bool p_use_debanding) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->use_debanding == p_use_debanding) {
		return;
	}
	viewport->use_debanding = p_use_debanding;
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_force_motion_vectors(RID p_viewport, bool p_force_motion_vectors) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->force_motion_vectors == p_force_motion_vectors) {
		return;
	}

	bool motion_vectors_before = _viewport_requires_motion_vectors(viewport);
	viewport->force_motion_vectors = p_force_motion_vectors;

	bool motion_vectors_after = _viewport_requires_motion_vectors(viewport);
	if (motion_vectors_before != motion_vectors_after) {
		num_viewports_with_motion_vectors += motion_vectors_after ? 1 : -1;
	}

	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_use_occlusion_culling(RID p_viewport, bool p_use_occlusion_culling) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->use_occlusion_culling == p_use_occlusion_culling) {
		return;
	}
	viewport->use_occlusion_culling = p_use_occlusion_culling;

	if (viewport->use_occlusion_culling) {
		RendererSceneOcclusionCull::get_singleton()->add_buffer(p_viewport);
		RendererSceneOcclusionCull::get_singleton()->buffer_set_scenario(p_viewport, viewport->scenario);
	} else {
		RendererSceneOcclusionCull::get_singleton()->remove_buffer(p_viewport);
	}

	viewport->occlusion_buffer_dirty = true;
}

void RendererViewport::viewport_set_occlusion_rays_per_thread(int p_rays_per_thread) {
	if (occlusion_rays_per_thread == p_rays_per_thread) {
		return;
	}

	occlusion_rays_per_thread = p_rays_per_thread;

	for (int i = 0; i < active_viewports.size(); i++) {
		active_viewports[i]->occlusion_buffer_dirty = true;
	}
}

void RendererViewport::viewport_set_occlusion_culling_build_quality(RS::ViewportOcclusionCullingBuildQuality p_quality) {
	RendererSceneOcclusionCull::get_singleton()->set_build_quality(p_quality);
}

void RendererViewport::viewport_set_mesh_lod_threshold(RID p_viewport, float p_pixels) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->mesh_lod_threshold = p_pixels;
}

int RendererViewport::viewport_get_render_info(RID p_viewport, RS::ViewportRenderInfoType p_type, RS::ViewportRenderInfo p_info) {
	ERR_FAIL_INDEX_V(p_type, RS::VIEWPORT_RENDER_INFO_TYPE_MAX, -1);
	ERR_FAIL_INDEX_V(p_info, RS::VIEWPORT_RENDER_INFO_MAX, -1);

	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	if (!viewport) {
		return 0; //there should be a lock here..
	}

	return viewport->render_info.info[p_type][p_info];
}

void RendererViewport::viewport_set_debug_draw(RID p_viewport, RS::ViewportDebugDraw p_draw) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	bool motion_vectors_before = _viewport_requires_motion_vectors(viewport);
	viewport->debug_draw = p_draw;

	bool motion_vectors_after = _viewport_requires_motion_vectors(viewport);
	if (motion_vectors_before != motion_vectors_after) {
		num_viewports_with_motion_vectors += motion_vectors_after ? 1 : -1;
	}
}

void RendererViewport::viewport_set_measure_render_time(RID p_viewport, bool p_enable) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->measure_render_time = p_enable;
}

float RendererViewport::viewport_get_measured_render_time_cpu(RID p_viewport) const {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, 0);

	return double(viewport->time_cpu_end - viewport->time_cpu_begin) / 1000.0;
}

float RendererViewport::viewport_get_measured_render_time_gpu(RID p_viewport) const {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, 0);

	return double((viewport->time_gpu_end - viewport->time_gpu_begin) / 1000) / 1000.0;
}

void RendererViewport::viewport_set_snap_2d_transforms_to_pixel(RID p_viewport, bool p_enabled) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	viewport->snap_2d_transforms_to_pixel = p_enabled;
}

void RendererViewport::viewport_set_snap_2d_vertices_to_pixel(RID p_viewport, bool p_enabled) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	viewport->snap_2d_vertices_to_pixel = p_enabled;
}

void RendererViewport::viewport_set_default_canvas_item_texture_filter(RID p_viewport, RS::CanvasItemTextureFilter p_filter) {
	ERR_FAIL_COND_MSG(p_filter == RS::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT, "Viewport does not accept DEFAULT as texture filter (it's the topmost choice already).)");
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->texture_filter = p_filter;
}
void RendererViewport::viewport_set_default_canvas_item_texture_repeat(RID p_viewport, RS::CanvasItemTextureRepeat p_repeat) {
	ERR_FAIL_COND_MSG(p_repeat == RS::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT, "Viewport does not accept DEFAULT as texture repeat (it's the topmost choice already).)");
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->texture_repeat = p_repeat;
}

void RendererViewport::viewport_set_sdf_oversize_and_scale(RID p_viewport, RS::ViewportSDFOversize p_size, RS::ViewportSDFScale p_scale) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	RSG::texture_storage->render_target_set_sdf_size_and_scale(viewport->render_target, p_size, p_scale);
}

RID RendererViewport::viewport_find_from_screen_attachment(DisplayServer::WindowID p_id) const {
	RID *rids = nullptr;
	uint32_t rid_count = viewport_owner.get_rid_count();
	rids = (RID *)alloca(sizeof(RID) * rid_count);
	viewport_owner.fill_owned_buffer(rids);
	for (uint32_t i = 0; i < rid_count; i++) {
		Viewport *viewport = viewport_owner.get_or_null(rids[i]);
		if (viewport->viewport_to_screen == p_id) {
			return rids[i];
		}
	}
	return RID();
}

void RendererViewport::viewport_set_vrs_mode(RID p_viewport, RS::ViewportVRSMode p_mode) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	RSG::texture_storage->render_target_set_vrs_mode(viewport->render_target, p_mode);
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_vrs_update_mode(RID p_viewport, RS::ViewportVRSUpdateMode p_mode) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	RSG::texture_storage->render_target_set_vrs_update_mode(viewport->render_target, p_mode);
}

void RendererViewport::viewport_set_vrs_texture(RID p_viewport, RID p_texture) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	RSG::texture_storage->render_target_set_vrs_texture(viewport->render_target, p_texture);
	_configure_3d_render_buffers(viewport);
}

bool RendererViewport::free(RID p_rid) {
	if (viewport_owner.owns(p_rid)) {
		Viewport *viewport = viewport_owner.get_or_null(p_rid);

		RSG::texture_storage->render_target_free(viewport->render_target);
		RSG::light_storage->shadow_atlas_free(viewport->shadow_atlas);
		if (viewport->render_buffers.is_valid()) {
			viewport->render_buffers.unref();
		}

		while (viewport->canvas_map.begin()) {
			viewport_remove_canvas(p_rid, viewport->canvas_map.begin()->key);
		}

		viewport_set_scenario(p_rid, RID());
		active_viewports.erase(viewport);
		sorted_active_viewports_dirty = true;

		if (viewport->use_occlusion_culling) {
			RendererSceneOcclusionCull::get_singleton()->remove_buffer(p_rid);
		}

		if (_viewport_requires_motion_vectors(viewport)) {
			num_viewports_with_motion_vectors--;
		}

		viewport_owner.free(p_rid);

		return true;
	}

	return false;
}

void RendererViewport::handle_timestamp(String p_timestamp, uint64_t p_cpu_time, uint64_t p_gpu_time) {
	RID *vp = timestamp_vp_map.getptr(p_timestamp);
	if (!vp) {
		return;
	}

	Viewport *viewport = viewport_owner.get_or_null(*vp);
	if (!viewport) {
		return;
	}

	if (p_timestamp.begins_with("vp_begin")) {
		viewport->time_cpu_begin = p_cpu_time;
		viewport->time_gpu_begin = p_gpu_time;
	}

	if (p_timestamp.begins_with("vp_end")) {
		viewport->time_cpu_end = p_cpu_time;
		viewport->time_gpu_end = p_gpu_time;
	}
}

void RendererViewport::viewport_set_canvas_cull_mask(RID p_viewport, uint32_t p_canvas_cull_mask) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	viewport->canvas_cull_mask = p_canvas_cull_mask;
}

// Workaround for setting this on thread.
void RendererViewport::call_set_vsync_mode(DisplayServer::VSyncMode p_mode, DisplayServer::WindowID p_window) {
	DisplayServer::get_singleton()->window_set_vsync_mode(p_mode, p_window);
}

int RendererViewport::get_total_objects_drawn() const {
	return total_objects_drawn;
}
int RendererViewport::get_total_primitives_drawn() const {
	return total_vertices_drawn;
}
int RendererViewport::get_total_draw_calls_used() const {
	return total_draw_calls_used;
}

int RendererViewport::get_num_viewports_with_motion_vectors() const {
	return num_viewports_with_motion_vectors;
}

RendererViewport::RendererViewport() {
	occlusion_rays_per_thread = GLOBAL_GET("rendering/occlusion_culling/occlusion_rays_per_thread");
}

// 保存当前的视图
void RendererViewport::save_current_view(Viewport* p_viewport) const {
	// 纹理的格式要拿出来看看
	RenderSceneBuffers *buffer_raw = p_viewport->render_buffers.ptr();
	RenderSceneBuffersRD *rd_raw = Object::cast_to<RenderSceneBuffersRD>(buffer_raw);
	if (rd_raw) {
		RID color_texture = rd_raw->get_texture(RB_SCOPE_BUFFERS, RB_TEX_COLOR);
		RD::TextureFormat texture_format = rd_raw->get_texture_format(RB_SCOPE_BUFFERS, RB_TEX_COLOR);
		Size2i texture_size = rd_raw->get_texture_slice_size(RB_SCOPE_BUFFERS, RB_TEX_COLOR, 0);
		RD::get_singleton()->save_texture_to_file(color_texture, 0, texture_format, texture_size, "user://color_buffer.png");

		RID depth_texture = rd_raw->get_texture(RB_SCOPE_BUFFERS, RB_TEX_DEPTH);
		RD::TextureFormat depth_format = rd_raw->get_texture_format(RB_SCOPE_BUFFERS, RB_TEX_DEPTH);
		Size2i depth_size = rd_raw->get_texture_slice_size(RB_SCOPE_BUFFERS, RB_TEX_DEPTH, 0);
		RD::get_singleton()->save_texture_to_file(depth_texture, 0, depth_format, depth_size, "user://depth_buffer.png");
	}
}

// 设置下一帧保存视图
void RendererViewport::set_to_save_next_frame()
{
	save_flag.store(true);
}
