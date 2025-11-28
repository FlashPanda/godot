#include "custom_post_effect.h"

void CustomPostEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_intensity", "intensity"), &CustomPostEffect::set_intensity);
	ClassDB::bind_method(D_METHOD("get_intensity"), &CustomPostEffect::get_intensity);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "intensity"), "set_intensity", "get_intensity");

	// 这些属性来自CompositorEffect，直接用就行
	// - effect_callback_type
	// - access_resolved_color
	// - access_resolved_depth
	// - needs_motion_vectors
	// - needs_normal_roughness
	// - needs_separate_specular
}

 CustomPostEffect::CustomPostEffect() {
	// 默认使用POST_TRANSPARENT:不影响透明物体之前的渲染
	set("effect_callback_type", (int)RS::COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);
	set("access_resolved_color", true);
	set("access_resolved_depth", false);

	rd_device = RenderingServer::get_singleton()->get_rendering_device();
}

CustomPostEffect::~CustomPostEffect() {
	_free_resources();
}

void CustomPostEffect::set_intensity(float p_intensity) {
	intensity = p_intensity;
}

float CustomPostEffect::get_intensity() const {
	return intensity;
}

void CustomPostEffect::_free_resources() {
	if (!rd_device) {
		return;
	}

	if (uniform_set_rid.is_valid()) {
		rd_device->free(uniform_set_rid);
		uniform_set_rid = RID();
	}

	if (pipeline_rid.is_valid()) {
		rd_device->free(pipeline_rid);
		pipeline_rid = RID();
	}

	if (shader_rid.is_valid()) {
		rd_device->free(shader_rid);
		shader_rid = RID();
	}

	if (sampler_rid.is_valid()) {
		rd_device->free(sampler_rid);
		sampler_rid = RID();
	}
}

void CustomPostEffect::_ensure_resources(const RenderData *p_render_data) {
	if (!rd_device) {
		rd_device = RenderingServer::get_singleton()->get_rendering_device();
	}
	if (!rd_device) {
		return;
	}

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
	if (!p_render_data) {
		return;
	}

	_ensure_resources(p_render_data);

	if (!rd_device || !shader_rid.is_valid() || !pipeline_rid.is_valid()) {
		return;
	}

	// 1. 从RenderData中取RenderSceneBuffers
	Ref<RenderSceneBuffers> buffers = p_render_data->get_render_scene_buffers();
	if (buffers.is_null())
		return;

	Ref<RenderSceneBuffersRD> buffers_rd = buffers;
	if (buffers_rd.is_null())
		return;

	// 2. 拿到当前的color texture RID
	RID color_texture = buffers_rd->get_internal_texture();

	// 3. 用color texture创建framebuffer
	RD::TextureFormat tex_format = rd_device->texture_get_format(color_texture);
	Vector2i size = buffers_rd->get_internal_size();
}