/**************************************************************************/
/*  renderer_geometry_instance.h                                          */
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

#ifndef RENDERER_GEOMETRY_INSTANCE_H
#define RENDERER_GEOMETRY_INSTANCE_H

#include "core/math/rect2.h"
#include "core/math/transform_3d.h"
#include "core/templates/rid.h"
#include "storage/utilities.h"

// API definition for our RenderGeometryInstance class so we can expose this through GDExtension in the near future
// RenderGeometryInstance 类的 API 定义，以便我们在不久的将来可以通过 GDExtension 对外开放。

/// 渲染视角下的几何实例
class RenderGeometryInstance {
public:
	virtual ~RenderGeometryInstance() {}

	virtual void _mark_dirty() = 0;

	// 骨骼
	virtual void set_skeleton(RID p_skeleton) = 0;
	// 材质
	virtual void set_material_override(RID p_override) = 0;
	virtual void set_material_overlay(RID p_overlay) = 0;
	virtual void set_surface_materials(const Vector<RID> &p_materials) = 0;
	virtual void set_mesh_instance(RID p_mesh_instance) = 0;
	virtual void set_transform(const Transform3D &p_transform, const AABB &p_aabb, const AABB &p_transformed_aabb) = 0;
	virtual void set_pivot_data(float p_sorting_offset, bool p_use_aabb_center) = 0;
	virtual void set_lod_bias(float p_lod_bias) = 0;
	virtual void set_layer_mask(uint32_t p_layer_mask) = 0;
	virtual void set_fade_range(bool p_enable_near, float p_near_begin, float p_near_end, bool p_enable_far, float p_far_begin, float p_far_end) = 0;
	virtual void set_parent_fade_alpha(float p_alpha) = 0;
	virtual void set_transparency(float p_transparency) = 0;
	virtual void set_use_baked_light(bool p_enable) = 0;
	virtual void set_use_dynamic_gi(bool p_enable) = 0;
	virtual void set_use_lightmap(RID p_lightmap_instance, const Rect2 &p_lightmap_uv_scale, int p_lightmap_slice_index) = 0;
	virtual void set_lightmap_capture(const Color *p_sh9) = 0;
	virtual void set_instance_shader_uniforms_offset(int32_t p_offset) = 0;
	virtual void set_cast_double_sided_shadows(bool p_enable) = 0;

	virtual Transform3D get_transform() = 0;
	virtual AABB get_aabb() = 0;

	virtual void pair_light_instances(const RID *p_light_instances, uint32_t p_light_instance_count) = 0;
	virtual void pair_reflection_probe_instances(const RID *p_reflection_probe_instances, uint32_t p_reflection_probe_instance_count) = 0;
	virtual void pair_decal_instances(const RID *p_decal_instances, uint32_t p_decal_instance_count) = 0;
	virtual void pair_voxel_gi_instances(const RID *p_voxel_gi_instances, uint32_t p_voxel_gi_instance_count) = 0;

	virtual void set_softshadow_projector_pairing(bool p_softshadow, bool p_projector) = 0;
};

// Base implementation of RenderGeometryInstance shared by internal renderers.
// 内部渲染器使用的渲染几何实例
// 一个网格（Mesh）在场景里要被渲染，除了原始几何数据外，还需要一大堆额外的状态，这个类就是用来存放这些状态的
class RenderGeometryInstanceBase : public RenderGeometryInstance {
public:
	// setup 用来做标记
	uint32_t base_flags = 0;
	uint32_t flags_cache = 0;

	// used during rendering
	// 渲染排序时用的深度值，通常是物体相对摄像机的深度。
	float depth = 0;

	// 这个就明显了，MeshInstance 渲染资源
	RID mesh_instance;

	// 世界变换矩阵
	Transform3D transform;
	// 是否是镜像实例（有时用在反射或负缩放的情况下）
	bool mirror = false;
	// 物体在世界坐标系下的包围盒，经过 transform 变换后的结果，用于剔除和排序
	AABB transformed_aabb;
	// 是否存在非均匀缩放（X、Y、Z 缩放比例不同），影响法线变换、光照计算等
	bool non_uniform_scale = false;
	// LOD（Level of Detail）相关的缩放系数，用来根据物体大小选择合适的 LOD
	float lod_model_scale = 1.0;
	// LOD 偏移，手动控制实例切换 LOD 的时机
	float lod_bias = 0.0;
	// 渲染排序的偏移量。比如两个平面重叠时，可以通过这个值来避免 Z-fighting。
	float sorting_offset = 0.0;
	// 决定排序石是用AABB中心点还是用其他参考点
	bool use_aabb_center = true;

	// 渲染层掩码，决定实例属于哪个渲染层，渲染摄像机可以选择只渲染某些层。
	uint32_t layer_mask = 1;

	/// 控制近裁剪面淡入淡出。例如靠近摄像机时逐渐透明。
	bool fade_near = false;
	float fade_near_begin = 0;
	float fade_near_end = 0;
	// 控制远裁剪面淡入淡出
	bool fade_far = false;
	float fade_far_begin = 0;
	float fade_far_end = 0;

	// 父节点的但如淡出透明度，用于继承上层的alpha
	float parent_fade_alpha = 1.0;
	// 强制透明度，用于控制最终渲染时的透明度
	float force_alpha = 1.0;

	// 在统一的shader uniform缓冲区里的偏移量，用来绑定实例级的shader参数
	int32_t shader_uniforms_offset = -1;

	// 内部数据结构，较少改动的属性
	struct Data {
		//data used less often goes into regular heap
		RID base;	// 基础资源的指针
		RS::InstanceType base_type;		// 实例的类型枚举，例如mesh，light，reflectionprobe等

		RID skeleton;	// 骨骼的RID
		Vector<RID> surface_materials;	// 每个子表面的材质列表
		RID material_override;	// 覆盖整个网格的材质
		RID material_overlay;	// 额外叠加的材质
		AABB aabb;	// 物体原始的AABB

		bool use_baked_light = false;		// 是否使用烘焙光照
		bool use_dynamic_gi = false;	// 是否使用实时全局光照
		bool cast_double_sided_shadows = false;	// 是否投射双面阴影
		bool dirty_dependencies = false;	// 标记依赖关系（材质、骨骼、光照探针等）是否需要更新

		DependencyTracker dependency_tracker;	// 引擎内部的依赖追踪器，用来追踪实例依赖了哪些资源
	};

	Data *data = nullptr;	// 网格的数据指针

	virtual void set_skeleton(RID p_skeleton) override;
	virtual void set_material_override(RID p_override) override;
	virtual void set_material_overlay(RID p_overlay) override;
	virtual void set_surface_materials(const Vector<RID> &p_materials) override;
	virtual void set_mesh_instance(RID p_mesh_instance) override;
	virtual void set_transform(const Transform3D &p_transform, const AABB &p_aabb, const AABB &p_transformed_aabb) override;
	virtual void set_pivot_data(float p_sorting_offset, bool p_use_aabb_center) override;
	virtual void set_lod_bias(float p_lod_bias) override;
	virtual void set_layer_mask(uint32_t p_layer_mask) override;
	virtual void set_fade_range(bool p_enable_near, float p_near_begin, float p_near_end, bool p_enable_far, float p_far_begin, float p_far_end) override;
	virtual void set_parent_fade_alpha(float p_alpha) override;
	virtual void set_transparency(float p_transparency) override;
	virtual void set_use_baked_light(bool p_enable) override;
	virtual void set_use_dynamic_gi(bool p_enable) override;
	virtual void set_instance_shader_uniforms_offset(int32_t p_offset) override;
	virtual void set_cast_double_sided_shadows(bool p_enable) override;

	virtual Transform3D get_transform() override;
	virtual AABB get_aabb() override;
};

#endif // RENDERER_GEOMETRY_INSTANCE_H
