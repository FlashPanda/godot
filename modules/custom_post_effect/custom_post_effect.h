#ifndef CUSTOM_POST_EFFECT_H
#define CUSTOM_POST_EFFECT_H

#include "scene/resources/compositor.h"							// CompositorEffect
#include "servers/rendering/rendering_device.h"					// RenderingDevice
#include "servers/rendering_server.h"							// RenderingServer
#include "servers/rendering/storage/render_data.h"				// RenderData
#include "servers/rendering/storage/render_scene_buffers.h"		// RenderSceneBuffers
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"		// RenderSceneBuffersRD

class CustomPostEffect : public CompositorEffect {
	GDCLASS(CustomPostEffect, CompositorEffect);

	// RD 资源
	RenderingDevice* rd_device = nullptr;

	RID shader_rid;
	RID pipeline_rid;
	RID sampler_rid;
	RID uniform_set_rid;

	float intensity = 1.0f;			// blur的强度

protected:
	static void _bind_methods();

	// 真正的回调（注意签名要和 CompositorEffect 里 GDVIRTUAL2 保持一致）
	void _render_callback(int p_effect_callback_type, const RenderData *p_render_data);

	void _ensure_resources(const RenderData* p_render_data);
	void _free_resources();

public:
	CustomPostEffect();
	~CustomPostEffect();
	void set_intensity(float p_intensity);
	float get_intensity() const;
};

#endif // CUSTOM_POST_EFFECT_H