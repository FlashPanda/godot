#include "custom_post_effect.h"

#include "scene/resources/3d/fog_material.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

void CustomPostEffect::_bind_methods() {
	// ClassDB::bind_method(D_METHOD("set_intensity", "intensity"), &CustomPostEffect::set_intensity);
	// ClassDB::bind_method(D_METHOD("get_intensity"), &CustomPostEffect::get_intensity);
	// ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "intensity"), "set_intensity", "get_intensity");

	// 这些属性来自CompositorEffect，直接用就行
	// - effect_callback_type
	// - access_resolved_color
	// - access_resolved_depth
	// - needs_motion_vectors
	// - needs_normal_roughness
	// - needs_separate_specular
	// 和 GDScript 的虚函数同名，这样 Compositor 会在渲染线程里回调到我们。
	ClassDB::bind_method(D_METHOD("_render_callback", "effect_callback_type", "render_data"),
			&CustomPostEffect::_render_callback);
}

 CustomPostEffect::CustomPostEffect() {
	// 设置回调类型
	set_effect_callback_type(EFFECT_CALLBACK_TYPE_POST_OPAQUE);

	// 等价于GDScripts的 RenderingServer.call_on_render_thread(_initialize_compute)
	RenderingServer* rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);
	rs->call_on_render_thread(callable_mp(this, &CustomPostEffect::_initialize_compute));
}
 void CustomPostEffect::_initialize_compute() {
	RenderingServer* rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);

	// 加载GLSL资源，根据实际情况调整
	Ref<RDShaderFile> shader_file = ResourceLoader::load("res://post_process_grayscale.glsl");
	ERR_FAIL_COND_MSG(shader_file.is_null(), "Failed to load shader file");

	Ref<RDShaderSPIRV> spirv = shader_file->get_spirv();
	shader_rid = RD::get_singleton()->shader_create_from_spirv(spirv->get_stages());

	if (shader_rid.is_valid()) {
		pipeline_rid = RD::get_singleton()->compute_pipeline_create(shader_rid);
	}
 }

 void CustomPostEffect::_notification(int p_what) {
	if (p_what == NOTIFICATION_PREDELETE) {
		if (shader_rid.is_valid()) {
			RD::get_singleton()->free(shader_rid);

			shader_rid = RID();
			pipeline_rid = RID();
		}
	}
}

CustomPostEffect::~CustomPostEffect() {
	_free_resources();
}

void CustomPostEffect::_free_resources() {
	if (!RD::get_singleton()) {
		return;
	}

	if (uniform_set_rid.is_valid()) {
		RD::get_singleton()->free(uniform_set_rid);
		uniform_set_rid = RID();
	}

	if (pipeline_rid.is_valid()) {
		RD::get_singleton()->free(pipeline_rid);
		pipeline_rid = RID();
	}

	if (shader_rid.is_valid()) {
		RD::get_singleton()->free(shader_rid);
		shader_rid = RID();
	}

	if (sampler_rid.is_valid()) {
		RD::get_singleton()->free(sampler_rid);
		sampler_rid = RID();
	}
}

void CustomPostEffect::_ensure_resources(const RenderData *p_render_data) {
	// if (!rd) {
	// 	rd = RenderingServer::get_singleton()->get_rendering_device();
	// }
	// if (!rd) {
	// 	return;
	// }

	// 如果还没创建shader/pipeline，在这里做一次性初始化
	if (!shader_rid.is_valid()) {
		// todo: 做初始化，从内置字符串/外部文件生成SPIR-V,然后创建shader
	}

	if (!pipeline_rid.is_valid()) {
		// todo: 创建一个全屏quad的pipeline
	}

	if (!sampler_rid.is_valid()) {
		// todo: 创建sampler，线性过滤+clamp
	}

	// uniform_set_rid可以在每帧/每分辨率变化的时候重新创建
}

void CustomPostEffect::_render_callback(int p_effect_callback_type, const RenderData* p_render_data) {
	if (!RD::get_singleton() || !pipeline_rid.is_valid()) {
		return;
	}

	if (p_effect_callback_type != EFFECT_CALLBACK_TYPE_POST_TRANSPARENT) {
		return;
	}

	Ref<RenderSceneBuffers> rsb = p_render_data->get_render_scene_buffers();
	Ref<RenderSceneBuffersRD> rsb_rd = Ref<RenderSceneBuffersRD>(rsb);
	if (rsb_rd.is_null()) {
		return;
	}

	Vector2i size = rsb_rd->get_internal_size();
	if (size.x == 0 && size.y == 0) {
		return;
	}

	int32_t x_groups = (size.x -1 ) / 8 + 1;
	int32_t y_groups = (size.y -1 ) / 8 + 1;
	int32_t z_groups = 1;

	// push constant: [size.x, size.y, 0, 0]
	float push_constant[4] = {
		static_cast<float>(size.x),
		static_cast<float>(size.y),
		0.f,
		0.f
	};

	int32_t view_count = rsb_rd->get_view_count();
	for (int32_t i = 0; i < view_count; i++) {

		RID input_image = rsb_rd->get_internal_texture(i);

		RenderingDevice::Uniform uniform;
		uniform.uniform_type = RenderingDevice::UNIFORM_TYPE_IMAGE;
		uniform.binding = 0;
		uniform.append_id(input_image);

		Vector<RenderingDevice::Uniform> uniforms;
		uniforms.push_back(uniform);

		RID uniform_set = RD::get_singleton()->uniform_set_create(uniforms, shader_rid, 0);

		RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipeline_rid);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
		RD::get_singleton()->compute_list_set_push_constant(
			compute_list,
			reinterpret_cast<const uint8_t* >(push_constant),
			sizeof(push_constant)
			);
		RD::get_singleton()->compute_list_dispatch(compute_list, x_groups, y_groups, z_groups);
		RD::get_singleton()->compute_list_end();
	}
}