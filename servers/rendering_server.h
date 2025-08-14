/**************************************************************************/
/*  rendering_server.h                                                    */
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

#ifndef RENDERING_SERVER_H
#define RENDERING_SERVER_H

#include "core/io/image.h"
#include "core/math/geometry_3d.h"
#include "core/math/transform_2d.h"
#include "core/templates/rid.h"
#include "core/variant/typed_array.h"
#include "core/variant/variant.h"
#include "servers/display_server.h"
#include "servers/rendering/rendering_device.h"

// Helper macros for code outside of the rendering server, but that is
// called by the rendering server.
#ifdef DEBUG_ENABLED
#define ERR_NOT_ON_RENDER_THREAD                                          \
	RenderingServer *rendering_server = RenderingServer::get_singleton(); \
	ERR_FAIL_NULL(rendering_server);                                      \
	ERR_FAIL_COND(!rendering_server->is_on_render_thread());
#define ERR_NOT_ON_RENDER_THREAD_V(m_ret)                                 \
	RenderingServer *rendering_server = RenderingServer::get_singleton(); \
	ERR_FAIL_NULL_V(rendering_server, m_ret);                             \
	ERR_FAIL_COND_V(!rendering_server->is_on_render_thread(), m_ret);
#else
#define ERR_NOT_ON_RENDER_THREAD
#define ERR_NOT_ON_RENDER_THREAD_V(m_ret)
#endif

class RenderingServer : public Object {
	GDCLASS(RenderingServer, Object);

	static RenderingServer *singleton;

	int mm_policy = 0;
	bool render_loop_enabled = true;


	/*
		用于将底层渲染管线中存储的原始表面数据（以字节向量形式的顶点、属性、蒙皮信息和索引数据）
		以及相关元信息（格式标志、顶点/索引长度、包围盒和 UV 缩放）解码并打包成一个 GDScript 可
		访问的 Variant Array。这个 Array 由多个子数组组成，每个子数组对应一类顶点属性（如位置、
		法线、切线、UV、骨骼权重/索引、索引列表等），并正是 mesh_surface_get_arrays() 等公开 API
		返回给用户的内容。
	*/
	Array _get_array_from_surface(uint64_t p_format,
		Vector<uint8_t> p_vertex_data,
		Vector<uint8_t> p_attrib_data,
		Vector<uint8_t> p_skin_data,
		int p_vertex_len,
		Vector<uint8_t> p_index_data,
		int p_index_len,
		const AABB &p_aabb,
		const Vector4 &p_uv_scale) const;

	// 二维和三维的比较容差
	const Vector2 SMALL_VEC2 = Vector2(CMP_EPSILON, CMP_EPSILON);
	const Vector3 SMALL_VEC3 = Vector3(CMP_EPSILON, CMP_EPSILON, CMP_EPSILON);

	// 获取当前引擎注册的所有全局 shader uniform 名称列表，可以给脚本语言用。
	virtual TypedArray<StringName> _global_shader_parameter_get_list() const;

protected:
	// 这一块感觉就是测试用的。
	RID _make_test_cube();
	void _free_internal_rids();		
	RID test_texture;
	RID white_texture;
	RID test_material;

	// 是将脚本层（GDScript/C#）传入的网格表面属性数组（顶点、法线、UV、骨骼等）
	// 解包、打包成底层渲染子系统可消费的字节流，并在此过程中计算出必要的元信息
	// （如包围盒、骨骼包围盒、UV 缩放），最后返回一个 Error 值指示执行结果或参数合法性
	Error _surface_set_data(Array p_arrays,
		uint64_t p_format,
		uint32_t *p_offsets,
		uint32_t p_vertex_stride,
		uint32_t p_normal_stride,
		uint32_t p_attrib_stride,
		uint32_t p_skin_stride,
		Vector<uint8_t> &r_vertex_array,
		Vector<uint8_t> &r_attrib_array,
		Vector<uint8_t> &r_skin_array,
		int p_vertex_array_len,
		Vector<uint8_t> &r_index_array,
		int p_index_array_len,
		AABB &r_aabb,
		Vector<AABB> &r_bone_aabb,
		Vector4 &r_uv_scale);

	static RenderingServer *(*create_func)();	// 创建函数指针
	static void _bind_methods();	// 绑定方法到脚本。

#ifndef DISABLE_DEPRECATED
	void _environment_set_fog_bind_compat_84792(RID p_env, bool p_enable, const Color &p_light_color, float p_light_energy, float p_sun_scatter, float p_density, float p_height, float p_height_density, float p_aerial_perspective, float p_sky_affect);
	void _canvas_item_add_multiline_bind_compat_84523(RID p_item, const Vector<Point2> &p_points, const Vector<Color> &p_colors, float p_width = -1.0);
	void _canvas_item_add_rect_bind_compat_84523(RID p_item, const Rect2 &p_rect, const Color &p_color);
	void _canvas_item_add_circle_bind_compat_84523(RID p_item, const Point2 &p_pos, float p_radius, const Color &p_color);

	static void _bind_compatibility_methods();
#endif

public:
	// 获取单例
	static RenderingServer *get_singleton();
	// 创建函数，其实就是调用上面的create_func
	static RenderingServer *create();

	enum {
		/*
			用来标记“无索引数组”或索引数组为空的错误状态。在调用需要索引数据的方法时，如果传入此值表示不使用索引
		*/
		NO_INDEX_ARRAY = -1,

		// 顶点骨骼权重数组的大小，表示每个顶点最多可受 4 根骨骼影响（常见于蒙皮动画的顶点数据布局）
		ARRAY_WEIGHTS_SIZE = 4,

		// 2D 画布节点（CanvasItem）的最小 Z 层，用于控制渲染顺序，不可低于该值
		CANVAS_ITEM_Z_MIN = -4096,

		// 2D 画布节点的最大 Z 层，用于控制渲染顺序，不可高于该值。
		CANVAS_ITEM_Z_MAX = 4096,

		// 后期处理辉光（Glow）效果所支持的最大级数（mip 级别数）
		MAX_GLOW_LEVELS = 7,

		// 引擎可支持的最大鼠标（或触控）光标数量（在实际渲染中此常量现已废弃，不再使用）
		MAX_CURSORS = 8,

		// 2D 场景中可同时存在的最大定向光数量，用于限制光照批处理
		MAX_2D_DIRECTIONAL_LIGHTS = 8,

		// 单个 Mesh 资源所能包含的最大子面（surface）数量，每个 surface 附带独立的顶点/索引数组和材质
		MAX_MESH_SURFACES = 256
	};

	/* TEXTURE API */

	// 纹理类型
	enum TextureType {
		TEXTURE_TYPE_2D,		// 2D纹理
		TEXTURE_TYPE_LAYERED,	// 分层纹理
		TEXTURE_TYPE_3D,		// 3D纹理
	};

	// 分层纹理类型
	enum TextureLayeredType {
		TEXTURE_LAYERED_2D_ARRAY,		// 2D数组
		TEXTURE_LAYERED_CUBEMAP,	// 六面体贴图
		TEXTURE_LAYERED_CUBEMAP_ARRAY,	// 六面体贴图数组
	};

	// 六面体贴图的各个方向
	enum CubeMapLayer {
		CUBEMAP_LAYER_LEFT,
		CUBEMAP_LAYER_RIGHT,
		CUBEMAP_LAYER_BOTTOM,
		CUBEMAP_LAYER_TOP,
		CUBEMAP_LAYER_FRONT,
		CUBEMAP_LAYER_BACK
	};

	// 纯虚：2D纹理创建
	virtual RID texture_2d_create(const Ref<Image> &p_image) = 0;
	// 纯虚：2D分层纹理创建
	virtual RID texture_2d_layered_create(const Vector<Ref<Image>> &p_layers, TextureLayeredType p_layered_type) = 0;
	// 纯虚：3D纹理创建
	virtual RID texture_3d_create(Image::Format, int p_width, int p_height, int p_depth, bool p_mipmaps, const Vector<Ref<Image>> &p_data) = 0; //all slices, then all the mipmaps, must be coherent
	// 纯虚：额外纹理创建
	// 用于创建一个“外部”纹理，底层不会分配或管理图像数据，而是将平台提供的硬件缓冲（如 Android 的 AHardwareBuffer 或 EGLImage）包装成一个可由 Godot 使用的纹理 RID
	virtual RID texture_external_create(int p_width, int p_height, uint64_t p_external_buffer = 0) = 0;
	// 纯虚：纹理代理创建
	// 曾用于创建一个“代理”纹理（ProxyTexture），可以在不复制底层图像的情况下包裹另一个纹理；但在 Godot 4 中此功能已移除，调用后什么也不做，并始终返回无效 RID。
	virtual RID texture_proxy_create(RID p_base) = 0;

	// 纯虚：从原生的句柄创建纹理
	virtual RID texture_create_from_native_handle(TextureType p_type, Image::Format p_format, uint64_t p_native_handle, int p_width, int p_height, int p_depth, int p_layers = 1, TextureLayeredType p_layered_type = TEXTURE_LAYERED_2D_ARRAY) = 0;

	//纯虚：2D纹理更新
	virtual void texture_2d_update(RID p_texture, const Ref<Image> &p_image, int p_layer = 0) = 0;
	// 纯虚：3D纹理更新
	virtual void texture_3d_update(RID p_texture, const Vector<Ref<Image>> &p_data) = 0;
	// 纯虚：额外纹理更新
	virtual void texture_external_update(RID p_texture, int p_width, int p_height, uint64_t p_external_buffer = 0) = 0;
	// 纯虚：代理纹理更新
	virtual void texture_proxy_update(RID p_texture, RID p_proxy_to) = 0;

	// These two APIs can be used together or in combination with the others.
	// Godot 的这三个纯虚方法分别用于在渲染服务器内部创建“占位”纹理（Placeholder Texture）对象，
	// 只会返回一个空的 RID 用于后续的纹理 API 调用，不会在 GPU 上传任何像素数据，主要用于项目以
	// 专用服务器模式导出、资源所在模块被禁用或实时延迟加载真实纹理等场景，以避免因缺少真实纹理而
	// 导致渲染失败或引擎崩溃。
	virtual RID texture_2d_placeholder_create() = 0;
	virtual RID texture_2d_layered_placeholder_create(TextureLayeredType p_layered_type) = 0;
	virtual RID texture_3d_placeholder_create() = 0;

	virtual Ref<Image> texture_2d_get(RID p_texture) const = 0;
	virtual Ref<Image> texture_2d_layer_get(RID p_texture, int p_layer) const = 0;
	virtual Vector<Ref<Image>> texture_3d_get(RID p_texture) const = 0;

	// 纯虚：替换纹理
	// 将已有纹理 p_texture 的底层数据替换为另一个纹理 p_by_texture，但保留原有的 RID 不变
	virtual void texture_replace(RID p_texture, RID p_by_texture) = 0;
	// 纯虚：设置纹理尺寸
	// 在不上传新像素数据的情况下，强制覆盖纹理 p_texture 的宽高元信息，使后续渲染时以新的尺寸进行采样和布局。
	virtual void texture_set_size_override(RID p_texture, int p_width, int p_height) = 0;

	// 纯虚：设置/获取纹理路径
	virtual void texture_set_path(RID p_texture, const String &p_path) = 0;
	virtual String texture_get_path(RID p_texture) const = 0;

	// 纯虚：获取纹理格式
	virtual Image::Format texture_get_format(RID p_texture) const = 0;

	// 纹理检查回调函数指针
	typedef void (*TextureDetectCallback)(void *);

	/*
	关于detect到底要detect什么？

	下面对“detect”回调的本质做一个剖析，帮助你理解它在 Godot 渲染管线中究竟“检测”的是什么。

## 一、检测回调的定位

Godot 的渲染服务器（`RenderingServer`）在上传或更新纹理资源时，会根据高层资源（如 `Texture2D`、`Texture3D`、`TextureLayered`）的用途，在渲染线程里对纹理做一系列“分类”处理——比如：

* 它是**3D 体积纹理**，还是普通的 2D 纹理？
* 它应当被当作**法线贴图**（normal map）来解码（线性空间、需要翻转绿通道）？
* 它应当被当作**粗糙度贴图**（roughness map）来读取单通道灰度？
* 它应当被当作**sRGB 纹理**来做伽马校正？

在这些判断发生的时刻，渲染服务器就会调用事先通过 `texture_set_detect_*_callback` 注册的函数，让上层（插件、编辑器、导入器等）有机会“听到”这一分类事件，并执行额外逻辑（如调整采样器设置、标记资源类型、触发编辑器刷新等）。

## 二、典型触发时机

1. **首次上传**：当你第一次调用 `texture_2d_create`、`texture_3d_create`、`texture_external_create` 等接口创建 GPU 纹理时。
2. **动态更新**：调用 `texture_replace`、`texture_update` 之类方法修改纹理数据后。
3. **导入流程**：Godot 在导入资源时，Importer 会给纹理 Resource 打上“这是法线贴图”“这是高光贴图”等标记，渲染服务器在看到这些标记时也会执行对应的检测回调。

只要渲染服务器内部判断出“这个 RID 对应的纹理应当被当作 X 类型处理”，就会触发 `texture_set_detect_X_callback` 注册的回调。

## 三、检测内容到底是什么？

* **语义分类**：不是对像素内容做图像分析，而是根据资源类型、导入标记或文件名后缀（如 `_normal`、`_roughness`）、压缩格式等元信息，对纹理“分类”。
* **用途判定**：将纹理送入适合的采样模式、解码空间、绑定点或渲染通道。例如：

  * 法线贴图要用线性采样、不做 sRGB 校正，并可能翻转绿通道；
  * sRGB 纹理要在采样时做伽马转换；
  * 3D 纹理要绑定到体积采样器；
* **回调目的**：让外部逻辑知晓“渲染服务器已经把这个纹理当作 X 用途”，从而可以

  * 在编辑器里高亮或标记该资源，
  * 在自定义渲染插件里调整后端状态，
  * 或者根据回调结果再做一次资源重配置（如切换过滤器、生成 MipMaps 等）。

---

### 调研参考

* Godot 官方文档：RenderingServer 接口列表，但未对 detect 回调做详细说明
* `servers/rendering_server.h`（GitHub 源码），包含 `texture_set_detect_*_callback` 定义，却无注释
* 社区讨论和 Issue，均未给出深入实现细节

由于公开文档和源码均未对“detect 回调”内部判断逻辑做详细注释，上述总结基于对渲染服务器整体设计和常见用法的解读。

	*/

	// 设置回调
	// 3D纹理和法线贴图识别
	virtual void texture_set_detect_3d_callback(RID p_texture, TextureDetectCallback p_callback, void *p_userdata) = 0;
	virtual void texture_set_detect_normal_callback(RID p_texture, TextureDetectCallback p_callback, void *p_userdata) = 0;

	/*
为什么会有这么多channel？

## 概要

Godot 的 `TextureDetectRoughnessChannel` 枚举用于在渲染服务器或导入流程中指定从哪一个颜色通道（R/G/B/A）或灰度（GRAY）提取粗糙度数据，从而兼容各种 PBR 贴图打包惯例，并生成更准确的粗糙度 mipmaps 以减少别名与视觉伪影。 ([docs.godotengine.org][1], [docs.godotengine.org][2], [github.com][3])

## PBR 通道打包背景

现代 PBR 工作流程中，为了节省纹理数、减少 Draw Call，常会将多种属性打包到一张贴图的不同通道。例如，Godot 的 ORM（Occlusion-Roughness-Metallic）贴图中：R 通道存储遮挡度、G 通道存储粗糙度、B 通道存储金属度。 ([docs.godotengine.org][1])

第三方工具（如 Unity、Substance Painter）也会将粗糙度（smoothness/roughness）存储在 metalness 贴图的 alpha 通道或其他通道中；不同项目或管线可能习惯不同通道。 ([reddit.com][4])

Godot 在导入器中提供 “Roughness > Mode” 选项，允许用户指定哪一个颜色通道作为粗糙度源，以兼容上述多种打包方式。 ([docs.godotengine.org][2])

## Godot 中的通道检测枚举

在底层，Godot 的渲染服务器通过 `TextureDetectRoughnessChannel` 枚举以及相关回调，决定在生成 mipmaps 或动态更新时，使用哪一次通道来提取粗糙度。 ([github.com][3])

```cpp
// servers/rendering_server.h
enum TextureDetectRoughnessChannel {
	TEXTURE_DETECT_ROUGHNESS_R,
	TEXTURE_DETECT_ROUGHNESS_G,
	TEXTURE_DETECT_ROUGHNESS_B,
	TEXTURE_DETECT_ROUGHNESS_A,
	TEXTURE_DETECT_ROUGHNESS_GRAY,
};
```

该枚举定义了五种类型：直接采样红、绿、蓝、或 alpha 通道，或使用灰度（平均或加权）通道。 ([github.com][3])

在资源导入器中，例如 `editor/import/resource_importer_texture.cpp`，Godot 会调用 `Image::generate_mipmap_roughness(p_roughness_channel, …)`，根据所选通道来生成专门优化的粗糙度 mipmaps，以减少视觉伪影并改善材质过渡。 ([github.com][5])

## 各枚举值含义

* **`TEXTURE_DETECT_ROUGHNESS_R`**：从纹理的红色通道读取粗糙度，适用于将粗糙度存储在 R 通道的自定义管线。 ([github.com][3])
* **`TEXTURE_DETECT_ROUGHNESS_G`**：从绿色通道读取，最常见于 ORM 贴图中的粗糙度。 ([github.com][3])
* **`TEXTURE_DETECT_ROUGHNESS_B`**：从蓝色通道读取，某些工作流会将粗糙度打包在 B 通道。 ([github.com][3])
* **`TEXTURE_DETECT_ROUGHNESS_A`**：从 alpha 通道读取，配合金属度贴图的 alpha 通道打包使用时非常常见。 ([docs.godotengine.org][2])
* **`TEXTURE_DETECT_ROUGHNESS_GRAY`**：对全图做灰度处理（如通道平均或加权），适用于单通道灰度粗糙度贴图。 ([github.com][3])

### 补充：与其他引擎的对比

类似 three.js 的 RoughnessMipmapper，也会基于法线贴图或指定通道生成自定义粗糙度 mipmaps，以改善反射预滤波效果；Godot 的通道检测机制则更通用，兼容更多打包方式。 ([github.com][6])

研究者也提出，利用法线贴图中的高频信息来动态生成或修正粗糙度 mipmaps，可以避免简单均值运算导致的细节丢失，突显了正确通道选择的重要性。 ([kosmonautblog.wordpress.com][7])

[1]: https://docs.godotengine.org/en/latest/tutorials/3d/standard_material_3d.html?utm_source=chatgpt.com "Standard Material 3D and ORM Material 3D - Godot Docs"
[2]: https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/importing_images.html "Importing images — Godot Engine (stable) documentation in English"
[3]: https://github.com/godotengine/godot/blob/master/servers/rendering_server.h?utm_source=chatgpt.com "godot/servers/rendering_server.h at master - GitHub"
[4]: https://www.reddit.com/r/godot/comments/61hurv/setting_up_texture_maps_for_pbr_workflow_in_godot/?utm_source=chatgpt.com "Setting up texture maps for PBR workflow in Godot 3? - Reddit"
[5]: https://github.com/godotengine/godot/blob/master/editor/import/resource_importer_texture.cpp?utm_source=chatgpt.com "godot/editor/import/resource_importer_texture.cpp at master - GitHub"
[6]: https://github.com/donmccurdy/glTF-Transform/issues/467?utm_source=chatgpt.com "Embed custom mipmaps in KTX2 roughness textures #467 - GitHub"
[7]: https://kosmonautblog.wordpress.com/2018/09/17/roughness-mip-maps-based-on-normal-maps/?utm_source=chatgpt.com "Roughness mip maps based on normal maps? - kosmonaut's blog"

	*/

	enum TextureDetectRoughnessChannel {
		TEXTURE_DETECT_ROUGHNESS_R,
		TEXTURE_DETECT_ROUGHNESS_G,
		TEXTURE_DETECT_ROUGHNESS_B,
		TEXTURE_DETECT_ROUGHNESS_A,
		TEXTURE_DETECT_ROUGHNESS_GRAY,
	};

	typedef void (*TextureDetectRoughnessCallback)(void *, const String &, TextureDetectRoughnessChannel);
	virtual void texture_set_detect_roughness_callback(RID p_texture, TextureDetectRoughnessCallback p_callback, void *p_userdata) = 0;

	// 纹理信息
	struct TextureInfo {
		RID texture;		// 资源ID
		uint32_t width;		// 宽
		uint32_t height;	// 高
		uint32_t depth;		// 深
		Image::Format format;	// 图像格式
		int64_t bytes;		// 字节数
		String path;		// 路径
	};

	// 纯虚：调试纹理
	virtual void texture_debug_usage(List<TextureInfo> *r_info) = 0;
	Array _texture_debug_usage_bind();

	// 纯虚：强制重新绘制纹理
	virtual void texture_set_force_redraw_if_visible(RID p_texture, bool p_enable) = 0;

	/*
这三条方法属于在 RenderingServer 与 RenderingDevice（低级渲染后端）之间进行 纹理互操作 和 句柄获取 的接口，它们并不直接从 Image 数据或外部缓冲创建完整的 Godot 纹理，而是用于：

在两套渲染 API 之间共享纹理资源（texture_rd_create / texture_get_rd_texture）

获取原生 GPU 纹理句柄 以便与第三方 API 交互（texture_get_native_handle）
	*/
	virtual RID texture_rd_create(const RID &p_rd_texture, const RenderingServer::TextureLayeredType p_layer_type = RenderingServer::TEXTURE_LAYERED_2D_ARRAY) = 0;
	virtual RID texture_get_rd_texture(RID p_texture, bool p_srgb = false) const = 0;
	virtual uint64_t texture_get_native_handle(RID p_texture, bool p_srgb = false) const = 0;

	/* PIPELINES API */

	enum PipelineSource {
		PIPELINE_SOURCE_CANVAS,		// 由 2D 画布渲染器（CanvasItem） 触发的管线编译，用于统计在执行 2D 绘制命令时新建 Shader 管线的次数
		PIPELINE_SOURCE_MESH,	// 在 加载或处理 Mesh 资源时触发的管线编译。当首次运行场景并需要为 Mesh 创建渲染管线时，该计数会增加
		PIPELINE_SOURCE_SURFACE,	// 在 构建表面缓存（Surface Cache） 以准备场景渲染之前触发的管线编译，该过程通常发生在场景加载阶段，可能会引起短暂卡顿
		PIPELINE_SOURCE_DRAW,	// 在实际执行 Draw Call 绘制场景时触发的管线编译，如果某材质或着色器管线尚未编译，则会在渲染时动态编译
		PIPELINE_SOURCE_SPECIALIZATION,	// 为了 优化当前场景 而在后台运行的管线专用化（Specialization）编译，这类编译不应阻塞主渲染线程，因此不会产生卡顿
		PIPELINE_SOURCE_MAX
	};

	/* SHADER API */
	// 着色器类型
	enum ShaderMode {
		SHADER_SPATIAL,			// 3D着色器
		SHADER_CANVAS_ITEM,		// 2D着色器
		SHADER_PARTICLES,		// 粒子着色器
		SHADER_SKY,				// 天空着色器
		SHADER_FOG,				// 雾着色器
		SHADER_MAX
	};

	// 剔除模式：禁用、剔除前面、剔除背面
	enum CullMode {
		CULL_MODE_DISABLED,
		CULL_MODE_FRONT,
		CULL_MODE_BACK,
	};

	// 纯虚：创建着色器
	virtual RID shader_create() = 0;
	// 纯虚：从代码创建着色器
	virtual RID shader_create_from_code(const String &p_code, const String &p_path_hint = String()) = 0;

	// 纯虚：设置着色器代码
	virtual void shader_set_code(RID p_shader, const String &p_code) = 0;
	// 纯虚：设置着色器路径提示
	virtual void shader_set_path_hint(RID p_shader, const String &p_path) = 0;
	// 纯虚：获取着色器的代码
	virtual String shader_get_code(RID p_shader) const = 0;
	// 纯虚：获取着色器的参数列表
	virtual void get_shader_parameter_list(RID p_shader, List<PropertyInfo> *p_param_list) const = 0;
	// 纯虚：获取指定 Shader 资源中某个 Uniform 参数的默认值
	virtual Variant shader_get_parameter_default(RID p_shader, const StringName &p_param) const = 0;

	// 纯虚：设置默认的纹理参数
	virtual void shader_set_default_texture_parameter(RID p_shader, const StringName &p_name, RID p_texture, int p_index = 0) = 0;
	// 纯虚：获取默认的纹理参数
	virtual RID shader_get_default_texture_parameter(RID p_shader, const StringName &p_name, int p_index = 0) const = 0;

	// 完整地保存一个 Shader 的多版本、多阶段原生源码，以便在不同后端（如 GLES、Vulkan、Metal 等）中按需编译和专用化。
	// 也就是说，后端不同，代码也是有差异的。
	struct ShaderNativeSourceCode {
		struct Version {
			struct Stage {
				String name;
				String code;
			};
			Vector<Stage> stages;
		};
		Vector<Version> versions;
	};

	// 纯虚：获取原生的着色器源码
	virtual ShaderNativeSourceCode shader_get_native_source_code(RID p_shader) const = 0;

	/* COMMON MATERIAL API */
	/* 通用材质API */ 

	// 材质的优先级数量
	// Godot 会先将所有透明对象按深度（back-to-front）排序，再在同一深度顺序中按 render_priority 从小到大绘制，数值越大越“靠后”被绘制（即越“在上面”）。
	enum {
		MATERIAL_RENDER_PRIORITY_MIN = -128,
		MATERIAL_RENDER_PRIORITY_MAX = 127,
	};

	// 纯虚：创建材质
	virtual RID material_create() = 0;
	// 纯虚：从着色器创建材质
	virtual RID material_create_from_shader(RID p_next_pass, int p_render_priority, RID p_shader) = 0;

	// 纯虚：设置着色器的材质
	virtual void material_set_shader(RID p_shader_material, RID p_shader) = 0;

	// 纯虚：设置材质的参数。
	virtual void material_set_param(RID p_material, const StringName &p_param, const Variant &p_value) = 0;
	// 纯虚：获取材质的某个参数的值
	virtual Variant material_get_param(RID p_material, const StringName &p_param) const = 0;

	// 纯虚：设置渲染优先级
	virtual void material_set_render_priority(RID p_material, int priority) = 0;

	// 纯虚：设置下一个材质
	// 用于在同一个材质上串联多个渲染通道（pass），也就是在渲染完当前材质后，紧接着使用另一个材质再进行一次绘制。
	// 形成了一个链式的多通道渲染流程，前一个材质的输出可以作为后一个材质的输入或背景
	virtual void material_set_next_pass(RID p_material, RID p_next_material) = 0;

	/* MESH API */

	// 数组类型
	enum ArrayType {
		// RGBA16这种格式到底映射成unorm还是snorm需要看引擎自己的实现

		ARRAY_VERTEX = 0, // RG32F (2D), RGB32F, RGBA16 (compressed)
							// 2D数组的数据格式是：RG32F，（float x 2)
							// 3D数组的格式是：RGB32F, (float x 3)
							// 3D压缩后的格式是：RGBA16，(uint16_t × 4，或 int16_t x 4)
		ARRAY_NORMAL = 1, // RG16, (int16_t x 2 或 uint16_t x 2)
		ARRAY_TANGENT = 2, // BA16 (with normal) or A16 (with vertex, when compressed)
							// (uint16或int16), (uint16或int16）
		ARRAY_COLOR = 3, // RGBA8
		ARRAY_TEX_UV = 4, // RG32F or RG16
		ARRAY_TEX_UV2 = 5, // RG32F or RG16
		ARRAY_CUSTOM0 = 6, // Depends on ArrayCustomFormat.	 依赖于数组自定义的格式。
		ARRAY_CUSTOM1 = 7,
		ARRAY_CUSTOM2 = 8,
		ARRAY_CUSTOM3 = 9,
		ARRAY_BONES = 10, // RGBA16UI (x2 if 8 weights)
							// RGBA16UI：4 通道×16 位无符号整数（uint16_t）
							// x2 if 8 weights：当每顶点骨骼权重超过 4 个，需要用两组该格式数组来存储最多 8 个索引
		ARRAY_WEIGHTS = 11, // RGBA16UNORM (x2 if 8 weights)
							// 顶点骨骼权重数组，格式为 RGBA16UNORM，每个通道是 16 位归一化无符号整数；若每顶点使用 8 个权重，则需要两组这样的权重数组（“x2 if 8 weights”）。
		ARRAY_INDEX = 12, // 16 or 32 bits depending on length > 0xFFFF.
							// 索引缓冲数组，用于绘制时从顶点数组中索引顶点。若顶点总数不超过 0xFFFF （65535），则使用 16 位无符号整数；超出时自动切换为 32 位无符号整数。
		ARRAY_MAX = 13
	};

	enum {
		ARRAY_CUSTOM_COUNT = ARRAY_BONES - ARRAY_CUSTOM0
	};

	// 自定义数据格式
	enum ArrayCustomFormat {
		ARRAY_CUSTOM_RGBA8_UNORM,	// uint8_t x 4
		ARRAY_CUSTOM_RGBA8_SNORM,	// int8_t x 4
		ARRAY_CUSTOM_RG_HALF,		// float16 x 2
		ARRAY_CUSTOM_RGBA_HALF,		// float16 x 4
		ARRAY_CUSTOM_R_FLOAT,		// float
		ARRAY_CUSTOM_RG_FLOAT,		// float x 2
		ARRAY_CUSTOM_RGB_FLOAT,		// float x 3
		ARRAY_CUSTOM_RGBA_FLOAT,	// float x 4
		ARRAY_CUSTOM_MAX
	};

	enum ArrayFormat : uint64_t {
		/* ARRAY FORMAT FLAGS */
		// 数组格式标记
		ARRAY_FORMAT_VERTEX = 1 << ARRAY_VERTEX,
		ARRAY_FORMAT_NORMAL = 1 << ARRAY_NORMAL,
		ARRAY_FORMAT_TANGENT = 1 << ARRAY_TANGENT,
		ARRAY_FORMAT_COLOR = 1 << ARRAY_COLOR,
		ARRAY_FORMAT_TEX_UV = 1 << ARRAY_TEX_UV,
		ARRAY_FORMAT_TEX_UV2 = 1 << ARRAY_TEX_UV2,
		ARRAY_FORMAT_CUSTOM0 = 1 << ARRAY_CUSTOM0,
		ARRAY_FORMAT_CUSTOM1 = 1 << ARRAY_CUSTOM1,
		ARRAY_FORMAT_CUSTOM2 = 1 << ARRAY_CUSTOM2,
		ARRAY_FORMAT_CUSTOM3 = 1 << ARRAY_CUSTOM3,
		ARRAY_FORMAT_BONES = 1 << ARRAY_BONES,
		ARRAY_FORMAT_WEIGHTS = 1 << ARRAY_WEIGHTS,
		ARRAY_FORMAT_INDEX = 1 << ARRAY_INDEX,

		// “蒙皮变形（Blend Shape）”数组允许携带的顶点通道掩码。这里表示只允许顶点、法线、切线三种数据
		ARRAY_FORMAT_BLEND_SHAPE_MASK = ARRAY_FORMAT_VERTEX | ARRAY_FORMAT_NORMAL | ARRAY_FORMAT_TANGENT,

		// 自定义通道
		// 最多 4 组自定义通道的格式信息以连续位块方式存储，避免为每组通道单独分配枚举值
		ARRAY_FORMAT_CUSTOM_BASE = (ARRAY_INDEX + 1),
		ARRAY_FORMAT_CUSTOM_BITS = 3,
		ARRAY_FORMAT_CUSTOM_MASK = 0x7,
		ARRAY_FORMAT_CUSTOM0_SHIFT = (ARRAY_FORMAT_CUSTOM_BASE + 0),
		ARRAY_FORMAT_CUSTOM1_SHIFT = (ARRAY_FORMAT_CUSTOM_BASE + ARRAY_FORMAT_CUSTOM_BITS),
		ARRAY_FORMAT_CUSTOM2_SHIFT = (ARRAY_FORMAT_CUSTOM_BASE + ARRAY_FORMAT_CUSTOM_BITS * 2),
		ARRAY_FORMAT_CUSTOM3_SHIFT = (ARRAY_FORMAT_CUSTOM_BASE + ARRAY_FORMAT_CUSTOM_BITS * 3),

		// 压缩位起始
		ARRAY_COMPRESS_FLAGS_BASE = (ARRAY_INDEX + 1 + 12),

		// 数组使用2D顶点
		ARRAY_FLAG_USE_2D_VERTICES = 1 << (ARRAY_COMPRESS_FLAGS_BASE + 0),
		// 数组使用动态更新
		ARRAY_FLAG_USE_DYNAMIC_UPDATE = 1 << (ARRAY_COMPRESS_FLAGS_BASE + 1),
		// 数组使用8骨骼权重
		ARRAY_FLAG_USE_8_BONE_WEIGHTS = 1 << (ARRAY_COMPRESS_FLAGS_BASE + 2),

		// 数组标记位空的顶点数组
		ARRAY_FLAG_USES_EMPTY_VERTEX_ARRAY = 1 << (ARRAY_COMPRESS_FLAGS_BASE + 3),

		// 压缩属性
		ARRAY_FLAG_COMPRESS_ATTRIBUTES = 1 << (ARRAY_COMPRESS_FLAGS_BASE + 4),
		// We leave enough room for up to 5 more compression flags.
		// 这里保留了足够的空间，预留5个压缩标记

		// mesh格式版本的标记
		ARRAY_FLAG_FORMAT_VERSION_BASE = ARRAY_COMPRESS_FLAGS_BASE + 10,
		ARRAY_FLAG_FORMAT_VERSION_SHIFT = ARRAY_FLAG_FORMAT_VERSION_BASE,
		// When changes are made to the mesh format, add a new version and use it for the CURRENT_VERSION.
		// 如果网格格式改变了，增加一个新的格式版本，并且将其设置位当前版本。
		ARRAY_FLAG_FORMAT_VERSION_1 = 0,
		ARRAY_FLAG_FORMAT_VERSION_2 = 1ULL << ARRAY_FLAG_FORMAT_VERSION_SHIFT,
		ARRAY_FLAG_FORMAT_CURRENT_VERSION = ARRAY_FLAG_FORMAT_VERSION_2,
		ARRAY_FLAG_FORMAT_VERSION_MASK = 0xFF, // 8 bits version
												// 用于在已经左移后的标志值中，隔离出那 8 位的版本号（& 0xFF）
	};

	// 静态断言，编译断言。
	static_assert(sizeof(ArrayFormat) == 8, "ArrayFormat should be 64 bits long.");

	// 五种图元类型：点、线、线带、三角形、三角形带
	enum PrimitiveType {
		PRIMITIVE_POINTS,
		PRIMITIVE_LINES,
		PRIMITIVE_LINE_STRIP,
		PRIMITIVE_TRIANGLES,
		PRIMITIVE_TRIANGLE_STRIP,
		PRIMITIVE_MAX,
	};

	// 单个表面数据
	// 以表面数据为单位进行绘制而不是一个object
	struct SurfaceData {
		PrimitiveType primitive = PRIMITIVE_MAX;

		uint64_t format = ARRAY_FLAG_FORMAT_CURRENT_VERSION;		// 数组格式的当前版本号
		Vector<uint8_t> vertex_data; // Vertex, Normal, Tangent (change with skinning, blendshape).
									// 顶点数据，包括顶点、法线、切线，会因为蒙皮改变，混合形状
		Vector<uint8_t> attribute_data; // Color, UV, UV2, Custom0-3.
									// 属性数据：颜色、uv、uv2、自定义0-3
		Vector<uint8_t> skin_data; // Bone index, Bone weight.
									// 蒙皮数据：骨骼索引、骨骼权重
		uint32_t vertex_count = 0;		// 顶点数量
		Vector<uint8_t> index_data;		// 索引数据
		uint32_t index_count = 0;		// 索引数量

		AABB aabb;		// 加速剔除的AABB包围盒
		struct LOD {	// 多级细节的索引集，按 edge_length 决定开启条件
			float edge_length = 0.0f;
			Vector<uint8_t> index_data;
		};
		Vector<LOD> lods;	// 所有LOD数据
		Vector<AABB> bone_aabbs;	// 骨骼在网格空间下的包围盒列表，用于可视化或剔除

		// Transforms used in runtime bone AABBs compute.
		// Since bone AABBs is saved in Mesh space, but bones is in Skeleton space.
		Transform3D mesh_to_skeleton_xform;	// 将网格顶点坐标转换到骨骼空间以计算 bone_aabbs

		Vector<uint8_t> blend_shape_data;	// 存储各 Blend Shape 增量顶点信息，可逐帧或插值变形

		Vector4 uv_scale;	// 局部 UV 缩放因子，常用于光照贴图或其它 UV 变换。
							// 这应该是有2个uv所以才是vector4

		RID material;	// 材质的ID
	};

	/*
	在 Godot 的底层渲染服务器（RenderingServer）中，SurfaceData 表示单个“面”（surface）的原始顶点/索引等数据结构，而 Mesh 则是一个资源（Resource），用于将若干个面组织在一起，并以一个可复用的 RID（资源 ID）进行管理和渲染。即使已有若干 SurfaceData，依然需要先创建一个 Mesh，然后将这些面添加到该 Mesh 中，才能在引擎中使用或渲染。
	*/

	// 纯虚：从表面创建mesh
	virtual RID mesh_create_from_surfaces(const Vector<SurfaceData> &p_surfaces, int p_blend_shape_count = 0) = 0;
	// 纯虚：创建mesh
	virtual RID mesh_create() = 0;


	/*
		Godot 中的 Blend Shape 数据以“顶点偏移量”形式（deltas）单独存储，
		不会直接写入或覆盖 Mesh 的原始顶点缓冲区。在运行时引擎会将这些偏移
		量按权重临时叠加到基础顶点上生成最终形变效果，基础 Mesh 数据始终保
		持不变；只有在显式“烘焙”到新 Mesh 时，变形结果才会真正写入顶点。
	*/

	// 用于告诉引擎：某个 Mesh 资源将包含多少个 Blend Shape（形状关键帧），以便在内部为每个 surface 分配相应的数据结构和内存。
	virtual void mesh_set_blend_shape_count(RID p_mesh, int p_blend_shape_count) = 0;

	// 返回由 array_index 指定的数组在其所属缓冲区开头的字节偏移量，方便区域更新或直接映射内存时定位起点。
	virtual uint32_t mesh_surface_get_format_offset(BitField<ArrayFormat> p_format, int p_vertex_len, int p_array_index) const;
	// 返回顶点缓冲区内相邻两顶点位置数据之间的字节距离（步幅）。
	virtual uint32_t mesh_surface_get_format_vertex_stride(BitField<ArrayFormat> p_format, int p_vertex_len) const;
	// 返回法线与切线联合数据在缓冲区内的字节步幅。尽管它们与顶点位置使用同一缓冲，但二者仅相互交错，因此步幅不同于位置数据。
	virtual uint32_t mesh_surface_get_format_normal_tangent_stride(BitField<ArrayFormat> p_format, int p_vertex_len) const;
	// 获取顶点属性（UV、顶点色、自定义属性等）数组在其缓冲区内的字节步幅
	virtual uint32_t mesh_surface_get_format_attribute_stride(BitField<ArrayFormat> p_format, int p_vertex_len) const;
	// 返回蒙皮数据（骨骼索引与权重）在缓冲区内的字节步幅，保证 GPU 按顶点顺序正确读取每个顶点的所有蒙皮影响值。
	virtual uint32_t mesh_surface_get_format_skin_stride(BitField<ArrayFormat> p_format, int p_vertex_len) const;

	/// Returns stride
	/*
		共同负责在脚本层的数组（Array、Packed*Array 等）与引擎内部的二进制网格数据（SurfaceData）
		之间相互转换，并提供对已有 Mesh 资源中各表面（surface）的几何、蒙皮、Blend Shape 和 LOD
		信息的查询
	*/

	/*
	根据 p_format（由 ArrayFormat 枚举按位或得到的位掩码）以及顶点数 p_vertex_len、索引数 p_index_len，计算各 ArrayType（顶点、法线、切线、颜色、UV、定制属性、骨骼索引、骨骼权重、索引等）在互联顶点缓冲区中的起始偏移，并返回总的顶点步长（stride）。同时，分别输出顶点位置、法线、属性（UV/颜色/自定义）和蒙皮数据的元素尺寸，以供底层渲染管线创建 GPU 缓冲使用
	*/
	virtual void mesh_surface_make_offsets_from_format(uint64_t p_format, int p_vertex_len, int p_index_len, uint32_t *r_offsets, uint32_t &r_vertex_element_size, uint32_t &r_normal_element_size, uint32_t &r_attrib_element_size, uint32_t &r_skin_element_size) const;
	// 从数组数据创建表面结构
	virtual Error mesh_create_surface_data_from_arrays(SurfaceData *r_surface_data, PrimitiveType p_primitive, const Array &p_arrays, const Array &p_blend_shapes = Array(), const Dictionary &p_lods = Dictionary(), uint64_t p_compress_format = 0);
	// 从表面数据创建数组
	Array mesh_create_arrays_from_surface_data(const SurfaceData &p_data) const;
	// 获取表面数据，我觉得可以用一个重载函数来完成，因为这个函数就是调用上面的东西
	// 或者如果要给脚本调用的化，那就不能重载
	Array mesh_surface_get_arrays(RID p_mesh, int p_surface) const;
	// 获取混合形状数组
	TypedArray<Array> mesh_surface_get_blend_shape_arrays(RID p_mesh, int p_surface) const;
	// 获取LOD字典
	Dictionary mesh_surface_get_lods(RID p_mesh, int p_surface) const;

	// 给网格增加表面数据，从数组获取数据
	virtual void mesh_add_surface_from_arrays(RID p_mesh, PrimitiveType p_primitive, const Array &p_arrays, const Array &p_blend_shapes = Array(), const Dictionary &p_lods = Dictionary(), BitField<ArrayFormat> p_compress_format = 0);
	// 把表面添加到网格
	virtual void mesh_add_surface(RID p_mesh, const SurfaceData &p_surface) = 0;

	// 获取混合形状数量
	virtual int mesh_get_blend_shape_count(RID p_mesh) const = 0;

	/*
		BlendShapeMode（归一化或相对模式）的设置作用于整个 Mesh 资源，而非单个 Surface。原因在于 Blend Shape 本质上是对网格整体顶点形状的插值或偏移，其权重计算需要跨越所有 Surface 进行统一处理。Surface 仅用于将几何数据按材质分组，但并不改变形状混合的统计方式，因此 BlendShapeMode 只能在 Mesh 级别定义。
	*/

	enum BlendShapeMode {
		BLEND_SHAPE_MODE_NORMALIZED,		// 对权重进行归一化处理，使多个形状的混合结果相当于各目标形状的线性插值
		BLEND_SHAPE_MODE_RELATIVE,			// 权重值直接作为相对于基准形状（base shape）的偏移量，相互之间不做归一化。
	};

	// 设置混合形状的模式
	virtual void mesh_set_blend_shape_mode(RID p_mesh, BlendShapeMode p_mode) = 0;
	// 获取混合形状的模式
	virtual BlendShapeMode mesh_get_blend_shape_mode(RID p_mesh) const = 0;

	// 更新表面的顶点区域
	virtual void mesh_surface_update_vertex_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) = 0;
	// 更新表面的属性区域
	virtual void mesh_surface_update_attribute_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) = 0;
	// 更新表面的蒙皮区域
	virtual void mesh_surface_update_skin_region(RID p_mesh, int p_surface, int p_offset, const Vector<uint8_t> &p_data) = 0;

	// 设置表面的材质
	virtual void mesh_surface_set_material(RID p_mesh, int p_surface, RID p_material) = 0;
	// 获取表面的材质
	virtual RID mesh_surface_get_material(RID p_mesh, int p_surface) const = 0;

	// 获取网格的表面数据
	virtual SurfaceData mesh_get_surface(RID p_mesh, int p_surface) const = 0;

	// 获取网格的表面数量
	virtual int mesh_get_surface_count(RID p_mesh) const = 0;

	/*
	为什么需要自定义 AABB
动态顶点偏移

当你在顶点着色器里根据世界位置或其他参数对顶点进行位移时，自动计算的 AABB 只包含原始网格，无法涵盖偏移后的部分，导致物体在视图中“消失”或误判可见性。

例如，GitHub 上就有人反馈同一个 Mesh 资源在不同实例上因着色器偏移而需要不同的包围盒，才不会错误剔除。
github.com

程序生成或变形网格

在运行时根据逻辑生成或修改顶点数据后，如果不重新计算 AABB，会无法正确反映新范围。手动指定能省去额外的顶点遍历开销。

统一控制内存与性能

对大型场景中大量共用同一 Mesh 的情况，用自定义 AABB 可以避免每帧自动扫描所有顶点，减轻 CPU 负担。
	*/

	// 设置网格的自定义AABB
	virtual void mesh_set_custom_aabb(RID p_mesh, const AABB &p_aabb) = 0;
	virtual AABB mesh_get_custom_aabb(RID p_mesh) const = 0;

	// 设置/获取路径
	virtual void mesh_set_path(RID p_mesh, const String &p_path) = 0;
	virtual String mesh_get_path(RID p_mesh) const = 0;

	// 设置阴影网格
	// 用简单的网格取产生阴影，这样的开销会小很多
	virtual void mesh_set_shadow_mesh(RID p_mesh, RID p_shadow_mesh) = 0;

	// 删除表面
	virtual void mesh_surface_remove(RID p_mesh, int p_surface) = 0;
	// 清空网格
	virtual void mesh_clear(RID p_mesh) = 0;

	/* MULTIMESH API */

	/*
MultiMesh 是 Godot 引擎中用于\*\*网格实例化（GPU Instancing）\*\*的专用资源。通过 `multimesh_create()`，你可以在 RenderingServer 上创建一个 MultiMesh 资源的句柄（RID），并在后续的所有 `multimesh_*` 接口中使用它。这个资源允许一次性提交成千上万的相同网格实例，只需一次绘制调用，大幅降低 API 调用开销，提高渲染性能。([docs.godot.community][1], [docs.godotengine.org][2])

## MultiMesh 资源概述

MultiMesh 是 Godot 提供的“低级网格实例化”解决方案，旨在替代大量单独的 `MeshInstance3D` 节点的逐个绘制方式。

* **批量实例化**：一次 API 调用即可渲染成千上万个网格实例，避免了重复的节点提交与渲染指令生成。([docs.godotengine.org][2], [docs.godotengine.org][3])
* **资源类型**：在 Godot 资源系统中，MultiMesh 继承自 `Resource`，可以在加载时缓存，也可在运行时通过 RenderingServer 动态创建与销毁。([docs.godot.community][1])

## multimesh\_create() 的作用

* **创建 RID**：调用 `RenderingServer.multimesh_create()` 会在渲染服务器内部生成一个空的 MultiMesh 资源，并返回其 RID。该 RID 是后续所有 `multimesh_*` 函数的唯一标识。([docs.godot.community][1], [github.com][4])
* **生命周期管理**：使用完毕后，需通过 `RenderingServer.free_rid(rid)` 手动释放，避免内存泄漏。([docs.godot.community][1])

## 为什么将多个 Mesh 视为一个资源

1. **减少绘制调用（Draw Call）数量**

   * 单独渲染每个 `MeshInstance3D` 会产生对应数量的 API 调用，随实例数量增加线性增长，严重消耗 CPU 开销。
   * MultiMesh 在 GPU 端使用硬件实例化，一次调用即可绘制所有实例，调用次数恒定为 1。([github.com][5], [reddit.com][6])
2. **共享几何数据**

   * 多个实例共享同一份顶点与索引数据，只需在实例缓冲区中更新变换矩阵或自定义数据，极大节约内存与带宽。([docs.godotengine.org][2])

## 使用场景示例

* **植被与草地**：在大面积地形上散布数千棵树或草丛，用 MultiMesh 绘制，性能几乎不受实例数量影响。([docs.godotengine.org][7])
* **粒子与小物件**：需渲染大量相同模型的粒子效果、子弹、石子等，使用 MultiMesh 可避免过多节点开销。([godotforums.org][8])

## 示例代码

```gdscript
# GDScript：创建并使用 MultiMesh
var mm_rid = RenderingServer.multimesh_create()             # 创建 RID
RenderingServer.multimesh_set_mesh(mm_rid, mesh.get_rid())  # 绑定基础网格
RenderingServer.multimesh_set_instance_count(mm_rid, 1000) # 设置实例数量

# 填充每个实例的变换矩阵
for i in range(1000):
	var xform = Transform3D(Basis(), Vector3(randf()*10,0,randf()*10))
	RenderingServer.multimesh_set_instance_transform(mm_rid, i, xform)

# 在场景中实例化
var instance_rid = RenderingServer.instance_create()
RenderingServer.instance_set_base(instance_rid, mm_rid)
```

上述代码中，`multimesh_create()` 返回的 RID 可用于后续所有 MultiMesh 操作，最终通过 `instance_set_base` 将其挂载到场景实例上。([github.com][4])

---

MultiMesh 将成百上千个相同网格实例打包成一个资源提交给渲染管线，是 Godot 中提升批量渲染性能的核心手段。通过 `multimesh_create()` 创建并管理该资源，即可显著降低 CPU 与 GPU 间的通信开销。

[1]: https://docs.godot.community/classes/class_renderingserver.html?utm_source=chatgpt.com "RenderingServer - Godot Docs"
[2]: https://docs.godotengine.org/en/stable/classes/class_multimesh.html?utm_source=chatgpt.com "MultiMesh — Godot Engine (stable) documentation in English"
[3]: https://docs.godotengine.org/en/4.3/classes/class_multimesh.html?utm_source=chatgpt.com "MultiMesh — Godot Engine (4.3) documentation in English"
[4]: https://github.com/godotengine/godot/blob/master/scene/resources/multimesh.cpp?utm_source=chatgpt.com "godot/scene/resources/multimesh.cpp at master - GitHub"
[5]: https://github.com/godotengine/godot/issues/17472?utm_source=chatgpt.com "MultiMesh: Support different material per instance #17472 - GitHub"
[6]: https://www.reddit.com/r/godot/comments/1fozyli/rendering_server_and_instancing/?utm_source=chatgpt.com "Rendering server and instancing : r/godot - Reddit"
[7]: https://docs.godotengine.org/en/3.1/tutorials/3d/using_multi_mesh_instance.html?utm_source=chatgpt.com "Using MultiMeshInstance - Godot Docs"
[8]: https://godotforums.org/d/36544-multimesh3d-via-renderingserver?utm_source=chatgpt.com "MultiMesh3D via RenderingServer - Godot Forums"

	*/
	virtual RID multimesh_create() = 0;

	// 变换的格式，是2D还是3D
	enum MultimeshTransformFormat {
		MULTIMESH_TRANSFORM_2D,
		MULTIMESH_TRANSFORM_3D,
	};

	// 物理插值时速度和质量的侧重
	enum MultimeshPhysicsInterpolationQuality {
		MULTIMESH_INTERP_QUALITY_FAST,
		MULTIMESH_INTERP_QUALITY_HIGH,
	};

	// 多网格分配数据空间
	virtual void multimesh_allocate_data(RID p_multimesh, int p_instances, MultimeshTransformFormat p_transform_format, bool p_use_colors = false, bool p_use_custom_data = false) = 0;
	// 多网格获取实例数量
	virtual int multimesh_get_instance_count(RID p_multimesh) const = 0;

	// 多网格设置单个网格
	virtual void multimesh_set_mesh(RID p_multimesh, RID p_mesh) = 0;
	// 设置实例的变换3D
	virtual void multimesh_instance_set_transform(RID p_multimesh, int p_index, const Transform3D &p_transform) = 0;
	// 设置实例的变换2D
	virtual void multimesh_instance_set_transform_2d(RID p_multimesh, int p_index, const Transform2D &p_transform) = 0;
	// 设置实例的颜色
	virtual void multimesh_instance_set_color(RID p_multimesh, int p_index, const Color &p_color) = 0;
	// 设置实例的自定义数据
	virtual void multimesh_instance_set_custom_data(RID p_multimesh, int p_index, const Color &p_color) = 0;

	// 获取多网格中的那个主网格资源ID
	virtual RID multimesh_get_mesh(RID p_multimesh) const = 0;
	// 获取多网格的AABB
	virtual AABB multimesh_get_aabb(RID p_multimesh) const = 0;

	// 设置/获取多网格中主网格的自定义aabb
	virtual void multimesh_set_custom_aabb(RID p_mesh, const AABB &p_aabb) = 0;
	virtual AABB multimesh_get_custom_aabb(RID p_mesh) const = 0;

	// 获取多网格实例的变换
	virtual Transform3D multimesh_instance_get_transform(RID p_multimesh, int p_index) const = 0;
	virtual Transform2D multimesh_instance_get_transform_2d(RID p_multimesh, int p_index) const = 0;
	virtual Color multimesh_instance_get_color(RID p_multimesh, int p_index) const = 0;
	virtual Color multimesh_instance_get_custom_data(RID p_multimesh, int p_index) const = 0;

	/*
	在 Godot 的渲染服务器（RenderingServer）中，multimesh_set_buffer、multimesh_get_buffer 和 multimesh_get_buffer_rd_rid 三个函数用于对 MultiMesh 的底层实例数据进行批量读写和获取其在渲染设备上的资源句柄。其中，multimesh_set_buffer 接受一个连续的浮点数组（Vector<float>），一次性上传所有实例的变换、颜色和自定义数据；multimesh_get_buffer 则以同样的格式将当前缓冲区数据复制回用户并返回；而 multimesh_get_buffer_rd_rid 则返回底层的渲染设备（RenderingDevice）缓冲区资源 ID，可用于直接绑定到 compute shader 或自定义渲染管线中
	*/

	virtual void multimesh_set_buffer(RID p_multimesh, const Vector<float> &p_buffer) = 0;
	virtual RID multimesh_get_buffer_rd_rid(RID p_multimesh) const = 0;
	virtual Vector<float> multimesh_get_buffer(RID p_multimesh) const = 0;

	// Interpolation.
	// 对buffer进行插值
	virtual void multimesh_set_buffer_interpolated(RID p_multimesh, const Vector<float> &p_buffer_curr, const Vector<float> &p_buffer_prev) = 0;
	// 对物理进行插值
	virtual void multimesh_set_physics_interpolated(RID p_multimesh, bool p_interpolated) = 0;
	// 物理插值，设置质量侧重
	virtual void multimesh_set_physics_interpolation_quality(RID p_multimesh, MultimeshPhysicsInterpolationQuality p_quality) = 0;
	// 重置物理插值
	virtual void multimesh_instance_reset_physics_interpolation(RID p_multimesh, int p_index) = 0;

	// 设置实例可见性
	virtual void multimesh_set_visible_instances(RID p_multimesh, int p_visible) = 0;
	virtual int multimesh_get_visible_instances(RID p_multimesh) const = 0;

	/* SKELETON API */

	// 创建骨架
	virtual RID skeleton_create() = 0;
	// 预先分配骨架的骨骼槽
	virtual void skeleton_allocate_data(RID p_skeleton, int p_bones, bool p_2d_skeleton = false) = 0;
	// 获取骨架的骨骼数
	virtual int skeleton_get_bone_count(RID p_skeleton) const = 0;
	// 设置骨骼的转换
	virtual void skeleton_bone_set_transform(RID p_skeleton, int p_bone, const Transform3D &p_transform) = 0;
	// 获取骨骼的转换
	virtual Transform3D skeleton_bone_get_transform(RID p_skeleton, int p_bone) const = 0;
	virtual void skeleton_bone_set_transform_2d(RID p_skeleton, int p_bone, const Transform2D &p_transform) = 0;
	virtual Transform2D skeleton_bone_get_transform_2d(RID p_skeleton, int p_bone) const = 0;
	// 设置骨架的基础2D转换
	virtual void skeleton_set_base_transform_2d(RID p_skeleton, const Transform2D &p_base_transform) = 0;

	/* Light API */
	// 光源类型还是单薄了点，没有面光源
	enum LightType {
		LIGHT_DIRECTIONAL,
		LIGHT_OMNI,
		LIGHT_SPOT
	};

	enum LightParam {
		LIGHT_PARAM_ENERGY,		// 光源强度
		LIGHT_PARAM_INDIRECT_ENERGY,	// 间接光强度
		LIGHT_PARAM_VOLUMETRIC_FOG_ENERGY,		// 体积雾强度
		LIGHT_PARAM_SPECULAR,	// 光源对表面镜面反射高光的影响强度
		LIGHT_PARAM_RANGE,	// 光源的作用范围（最大距离），超过此距离光照消失
		LIGHT_PARAM_SIZE,	// 物理尺寸，对点光源和聚光灯来说
		LIGHT_PARAM_ATTENUATION,	// 衰减系数
		LIGHT_PARAM_SPOT_ANGLE,		// 聚光灯角度
		LIGHT_PARAM_SPOT_ATTENUATION,	// 聚光灯衰减系数
		LIGHT_PARAM_SHADOW_MAX_DISTANCE,	// 阴影最大距离
		LIGHT_PARAM_SHADOW_SPLIT_1_OFFSET,	// 阴影图集分割占比（第1段）
		LIGHT_PARAM_SHADOW_SPLIT_2_OFFSET,
		LIGHT_PARAM_SHADOW_SPLIT_3_OFFSET,
		LIGHT_PARAM_SHADOW_FADE_START,		// 阴影开始渐隐的距离占比（相对于最大阴影距离）
		LIGHT_PARAM_SHADOW_NORMAL_BIAS,		// 法线偏差，用于沿法线方向偏移深度采样位置，减少自阴影伪影
		LIGHT_PARAM_SHADOW_BIAS,	// 深度偏差，用于修正自阴影伪影
		LIGHT_PARAM_SHADOW_PANCAKE_SIZE,	// 平行光专用“阴影煎饼”尺寸，向前偏移阴影相机近平面以提高深度分辨率；过大可能导致边缘伪影
		LIGHT_PARAM_SHADOW_OPACITY,	// 阴影不透明度，低于 1.0 时光源可部分穿透阴影，可用于模拟全局光照效果
		LIGHT_PARAM_SHADOW_BLUR,	// 阴影边缘模糊程度，减少低分辨率阴影图集锯齿；过高值可能产生颗粒化
		LIGHT_PARAM_TRANSMITTANCE_BIAS,	// 透射偏差，目前官方文档未提供详细说明
		LIGHT_PARAM_INTENSITY,	// 物理光照强度：点光和聚光以流明（Lumens）计，平行光以勒克斯（Lux）计，仅在开启物理光照单位时生效
		LIGHT_PARAM_MAX
	};

	// 纯虚：创建定向光
	virtual RID directional_light_create() = 0;
	// 纯虚：创建点光源
	virtual RID omni_light_create() = 0;
	// 纯虚：创建聚光灯
	virtual RID spot_light_create() = 0;

	// 纯虚：设置光源颜色
	virtual void light_set_color(RID p_light, const Color &p_color) = 0;
	// 纯虚：设置参数，参数枚举就是上面的一个枚举
	virtual void light_set_param(RID p_light, LightParam p_param, float p_value) = 0;
	// 纯虚：设置是否启用阴影
	virtual void light_set_shadow(RID p_light, bool p_enabled) = 0;
	// 纯虚：为指定的 3D 光源设置一个投影纹理，使光照呈现“投影仪”效果，即模拟光线穿过有颜色但半透明的图案（如彩色玻璃）后投射到场景中。投影纹理要求 shadow_enabled 为开启状态，否则无效
	virtual void light_set_projector(RID p_light, RID p_texture) = 0;
	// 纯虚：启用“负光”模式：当 p_enable = true 时，光源将从场景中减去光照而非添加，用于制造凹陷或“挖空”效果，也可将明暗反转，产生发光轮廓。
	virtual void light_set_negative(RID p_light, bool p_enable) = 0;
	// 纯虚：设置光源的 剔除掩码（cull mask），该掩码对应节点的可见层（layers）。只有与掩码位与运算后非零的对象才会受到此光源照射，从而实现光照层级过滤
	virtual void light_set_cull_mask(RID p_light, uint32_t p_mask) = 0;
	// 纯虚：控制光照及阴影随距离的渐隐效果
	virtual void light_set_distance_fade(RID p_light, bool p_enabled, float p_begin, float p_shadow, float p_length) = 0;
	// 纯虚：反转光源或其阴影体积的剔除面模式：启用后，渲染光照体积或阴影贴图时会将正面/背面剔除规则翻转，适用于双面或特殊剔除需求
	virtual void light_set_reverse_cull_face_mode(RID p_light, bool p_enabled) = 0;
	// 纯虚：设置阴影投射掩码（shadow caster mask），只有与掩码位与运算后非零的对象才会为此光源投射实时阴影，从而实现对阴影投射对象的精细过滤以节省性能。
	virtual void light_set_shadow_caster_mask(RID p_light, uint32_t p_caster_mask) = 0;

	// 枚举用于控制 Godot 中光源在各种烘焙流程（如光照贴图 LightmapGI、体素全局光照 VoxelGI、和 SDFGI）的参与方式
	enum LightBakeMode {
		LIGHT_BAKE_DISABLED,	// 在烘焙贴图阶段忽略该光源，但仍会在全局光照管线中生效，适合频繁变化的动态光源
		LIGHT_BAKE_STATIC,		// 参与静态烘焙，烘焙结果不随实时移动或修改更新，适合略微闪烁等小幅度变化
		LIGHT_BAKE_DYNAMIC,		// 在允许实时更新的管线（VoxelGI 和 SDFGI）中开启光源全局光照的动态更新，但性能开销高于静态模式
	};

	virtual void light_set_bake_mode(RID p_light, LightBakeMode p_bake_mode) = 0;
	// 设置最大的SDFGI的级联级数
	virtual void light_set_max_sdfgi_cascade(RID p_light, uint32_t p_cascade) = 0;

	// Omni light
	// 指定全向光（OmniLight/PointLight）在实时阴影渲染时所使用的映射算法：Dual Paraboloid 还是 Cube Map
	enum LightOmniShadowMode {
		// Dual Paraboloid 阴影映射通过将场景沿光源位置分成前、后两个抛物面投影并渲染到两张纹理上，再在着色阶段合并深度信息，渲染速度较快。
		// 该方法仅需两次渲染通道，相比 Cube Map 性能开销更低，但在视野更大或精度要求较高的场景中容易出现边缘形变和扭曲。
		LIGHT_OMNI_SHADOW_DUAL_PARABOLOID,

		// Cube Map 阴影映射将场景渲染到由六个面组成的立方体贴图中，可完整捕捉光源周围各方向的深度信息，从而获得更高精度且畸变更少的阴影。
		// 该方法需六次渲染通道，性能开销约为 Dual Paraboloid 的三倍，但在阴影质量和形变控制方面更具优势。
		LIGHT_OMNI_SHADOW_CUBE,
	};

	// 设置点光源的阴影模式
	virtual void light_omni_set_shadow_mode(RID p_light, LightOmniShadowMode p_mode) = 0;

	// Directional light
	// 定义了平行光（DirectionalLight3D）在 Godot 中实时阴影生成时的投影方式，包括普通正交投影和基于级联分割（Cascaded Shadow Maps）的平行投影模式
	// 用户可以根据阴影质量需求与性能预算，在正交模式、2 级级联模式或4 级级联模式三种之间切换，以在画面质量与性能消耗间取得平衡
	enum LightDirectionalShadowMode {
		// 使用正交投影（Orthogonal Projection）来生成阴影贴图，不进行任何分割，阴影贴图覆盖整个视锥体，但容易出现远处分辨率不足的问题
		LIGHT_DIRECTIONAL_SHADOW_ORTHOGONAL,
		// 采用 2 级级联（2 splits）的方式，将视锥体分为两个距离范围（近/远），分别渲染到两张深度贴图以提高近距离阴影精度，同时将远处阴影合并至第二级
		LIGHT_DIRECTIONAL_SHADOW_PARALLEL_2_SPLITS,
		// 用 4 级级联（4 splits）的方式，将视锥体细分为四个距离范围，分别渲染到四张深度贴图，大幅提升不同深度区间的阴影分辨率和稳定性，常用于对阴影质量要求极高的场景
		LIGHT_DIRECTIONAL_SHADOW_PARALLEL_4_SPLITS,
	};

	// 控制 DirectionalLight3D（或底层的 RenderingServer）节点在场景光照（Scene Lighting）和天空渲染（Sky Rendering）中的可见性与作用方式
	enum LightDirectionalSkyMode {
		LIGHT_DIRECTIONAL_SKY_MODE_LIGHT_AND_SKY,	// 同时用于 场景光照 和 天空渲染。既会投射方向光照射场景，也会在天空（如天空盒、天空着色器）中可见
		LIGHT_DIRECTIONAL_SKY_MODE_LIGHT_ONLY,		// 仅用于 场景光照（包括直接照明与全局光照），但在天空渲染中不可见。适用于需要光照效果但不希望“太阳”在天空中可见的场景
		LIGHT_DIRECTIONAL_SKY_MODE_SKY_ONLY,		// 仅用于 天空渲染，不对场景产生光照（既不投射直接光，也不参与全局光照）。可用于只想调整天空效果而不影响场景光照的情境，例如夜间模式下仅控制天体位置和颜色
	};

	// 该方法为指定的方向光设置阴影投影模式，用以决定使用哪种级联或非级联的阴影映射方式
	virtual void light_directional_set_shadow_mode(RID p_light, LightDirectionalShadowMode p_mode) = 0;
	// 启用或禁用在多级联阴影贴图之间的平滑混合，从而减少分区边界处的明显切换痕迹
	virtual void light_directional_set_blend_splits(RID p_light, bool p_enable) = 0;
	// 控制方向光在场景光照与天空渲染中的可见性，可灵活切换其对两者的影响
	virtual void light_directional_set_sky_mode(RID p_light, LightDirectionalSkyMode p_mode) = 0;

	// Shadow atlas
	// 阴影图集

	// 创建阴影图集
	virtual RID shadow_atlas_create() = 0;
	// 设置阴影图集的尺寸大小以及是否使用16位的数据格式。
	virtual void shadow_atlas_set_size(RID p_atlas, int p_size, bool p_use_16_bits = true) = 0;
	// 为位置光源（Omni/Spot 光）的阴影图集（Shadow Atlas）中指定的象限设置细分数，从而决定该象限可划分出多少个子格来存放不同大小、不同距离的阴影贴图
	virtual void shadow_atlas_set_quadrant_subdivision(RID p_atlas, int p_quadrant, int p_subdivision) = 0;
	// 方向光阴影图集尺寸设置，并且指定是不是用16位。
	virtual void directional_shadow_atlas_set_size(int p_size, bool p_16_bits = true) = 0;


	/**
		阴影过滤（Shadow Filtering）的质量级别，用于在“阴影边缘软化程度”（shadow blur）和渲染性能之间进行权衡。不同的枚举值对应不同的采样数量和模糊半径，使得开发者可以根据目标平台或画面需求选择更快或更精细的阴影效果。
	 */
	enum ShadowQuality {
		SHADOW_QUALITY_HARD,	// 最低的阴影过滤质量（最快），产生硬阴影
		SHADOW_QUALITY_SOFT_VERY_LOW,	// 非常低的软阴影过滤质量（更快）。在此模式下，shadow_blur 会自动乘以 0.75× 以减少噪点（仅对 light_size 或 light_angular_distance 为 0.0 的光源生效）
		SHADOW_QUALITY_SOFT_LOW,	// 低软阴影过滤质量（快速）
		SHADOW_QUALITY_SOFT_MEDIUM,	// 中等软阴影过滤质量（平均）
		SHADOW_QUALITY_SOFT_HIGH,	// 高软阴影过滤质量（较慢）。在此模式下，shadow_blur 会自动乘以 1.5× 以更好利用高采样数，并提高动态物体阴影的稳定性（仅对 light_size 或 light_angular_distance 为 0.0 的光源生效）。
		SHADOW_QUALITY_SOFT_ULTRA,	// 最高软阴影过滤质量（最慢）。在此模式下，shadow_blur 会自动乘以 2×，以最大化采样效果并增强动态阴影稳定性（同样仅在特定光源条件下应用倍增）。
		SHADOW_QUALITY_MAX
	};

	// 准点光源的阴影质量设置（点光源、聚光灯）
	virtual void positional_soft_shadow_filter_set_quality(ShadowQuality p_quality) = 0;
	// 方向光的阴影质量设置
	virtual void directional_soft_shadow_filter_set_quality(ShadowQuality p_quality) = 0;

	// 投影灯（Light Projector）所使用的纹理采样过滤模式
	enum LightProjectorFilter {
		LIGHT_PROJECTOR_FILTER_NEAREST,	// 最近邻采样，不使用 Mipmaps，投影纹理在距离较远时看起来锐利但颗粒感强，适合像素风场景，性能与使用 Mipmaps 相当
		LIGHT_PROJECTOR_FILTER_LINEAR,	// 线性插值采样，不使用 Mipmaps，投影纹理在距离较远时看起来平滑但略显模糊，适合写实或高分辨率贴图，性能与使用 Mipmaps 相当。
		LIGHT_PROJECTOR_FILTER_NEAREST_MIPMAPS,		// 最近邻采样 + 各向同性 Mipmaps，距离远时会自动切换到低分辨率 Mipmap 以减少闪烁，视觉效果平滑、略有模糊，性能与不使用 Mipmaps 相当
		LIGHT_PROJECTOR_FILTER_LINEAR_MIPMAPS,		// 线性插值 + 各向同性 Mipmaps，结合线性过渡和 Mipmaps，可在远处保持平滑渐变但更加模糊，性能与不使用 Mipmaps 相当。
		LIGHT_PROJECTOR_FILTER_NEAREST_MIPMAPS_ANISOTROPIC,		// 最近邻 + 各向异性 Mipmaps，在斜角视角下依然保持纹理清晰，适合对角度变化敏感的投影，视觉效果优于各向同性 Mipmaps，但计算开销更大。各向异性级别由anisotropic_filtering_level 决定。
		LIGHT_PROJECTOR_FILTER_LINEAR_MIPMAPS_ANISOTROPIC,	// 线性插值 + 各向异性 Mipmaps，兼具平滑过渡与斜角清晰度，适用于高质量需求场景，开销也最高，同样受项目设置中各向异性级别控制。
	};

	// 设置滤波模式
	virtual void light_projectors_set_filter(LightProjectorFilter p_filter) = 0;

	/* PROBE API */
	// 探针API

	// 纯虚：创建反射探针
	virtual RID reflection_probe_create() = 0;

	// 反射探针的更新模式
	enum ReflectionProbeUpdateMode {
		REFLECTION_PROBE_UPDATE_ONCE,		// 只更新一次
		REFLECTION_PROBE_UPDATE_ALWAYS,		// 总是更新
	};

	// 设置反射探针的更新模式
	virtual void reflection_probe_set_update_mode(RID p_probe, ReflectionProbeUpdateMode p_mode) = 0;
	// 设置探针反射效果的强度，对应 ReflectionProbe.intensity 属性。
	virtual void reflection_probe_set_intensity(RID p_probe, float p_intensity) = 0;
	// 设置探针与场景其余部分混合的距离，对应 ReflectionProbe.blend_distance 属性。
	virtual void reflection_probe_set_blend_distance(RID p_probe, float p_blend_distance) = 0;


	// 控制环境光如何在探针盒子内应用
	enum ReflectionProbeAmbientMode {
		REFLECTION_PROBE_AMBIENT_DISABLED,		// 探针盒子内不添加任何环境光。开启此模式时，区域内的物体将仅受直接光和其它全局光照（如 VoxelGI、SDFGI）或天空盒影响，而不额外增加环境漫反射
		REFLECTION_PROBE_AMBIENT_ENVIRONMENT,	// 探针盒子内自动采样环境光：从环境天空（WorldEnvironment 中 Sky 资源）和探针的反射平均色中获取环境光颜色，并将其应用于区域内所有物体。适用于希望局部环境能继承全局天空光照效果的场景
		REFLECTION_PROBE_AMBIENT_COLOR,		// 使用用户在 ambient_color 和 ambient_color_energy 属性中指定的自定义颜色及强度作为环境光。该模式可作为近似的面光源（area lighting）模拟，允许在室内或特殊区域内灵活设置暖色、冷色等氛围光效果
	};

	// 设置反射探针的环境光模式
	virtual void reflection_probe_set_ambient_mode(RID p_probe, ReflectionProbeAmbientMode p_mode) = 0;
	// 设置探针内部使用的自定义环境光颜色
	virtual void reflection_probe_set_ambient_color(RID p_probe, const Color &p_color) = 0;
	// 设置自定义环境光的强度（能量），影响环境光色值的最终亮度
	virtual void reflection_probe_set_ambient_energy(RID p_probe, float p_energy) = 0;
	// 设置物体距离探针中心的最大反射距离，超过此距离的物体将被剔除以提升性能
	virtual void reflection_probe_set_max_distance(RID p_probe, float p_distance) = 0;
	// 设置探针捕捉区域的尺寸（盒体大小），区域越大覆盖范围越广但会降低感知分辨率
	virtual void reflection_probe_set_size(RID p_probe, const Vector3 &p_size) = 0;
	// 在盒状投影模式下设置探针中心的偏移量，用于更好地适配不规则或旋转房间的反射
	virtual void reflection_probe_set_origin_offset(RID p_probe, const Vector3 &p_offset) = 0;
	// 如果启用，探针将忽略环境天空光，仅基于内部环境光渲染反射
	virtual void reflection_probe_set_as_interior(RID p_probe, bool p_enable) = 0;
	// 启用盒状投影，可根据相机位置动态调整反射中心，使长方形房间的反射更准确
	virtual void reflection_probe_set_enable_box_projection(RID p_probe, bool p_enable) = 0;
	// 反射探针启用阴影
	virtual void reflection_probe_set_enable_shadows(RID p_probe, bool p_enable) = 0;
	// 反射探针设置剔除掩码
	virtual void reflection_probe_set_cull_mask(RID p_probe, uint32_t p_layers) = 0;
	// 反射探针设置反射掩码
	virtual void reflection_probe_set_reflection_mask(RID p_probe, uint32_t p_layers) = 0;
	// 反射探针设置分辨率
	virtual void reflection_probe_set_resolution(RID p_probe, int p_resolution) = 0;
	// 反射探针设置LOD阈值
	virtual void reflection_probe_set_mesh_lod_threshold(RID p_probe, float p_pixels) = 0;

	/* DECAL API */

	/*
		贴花本质上时一个带有AABB（轴对齐包围盒）的投影纹理，其所有纹理都会被自动打包到一个纹理图集中，并在渲染时与场景网格一起集群绘制。
		从而实现高效的实时贴花效果。
	*/

	// 贴花（Decal）所使用的各类纹理槽，包括反照率、法线、遮蔽/粗糙度/金属度以及自发光等
	enum DecalTexture {
		DECAL_TEXTURE_ALBEDO,	// 贴花的反照率（Albedo/漫反射颜色）纹理槽，对应 API 属性 Decal.texture_albedo，用于定义投影到表面上的基础色彩
		DECAL_TEXTURE_NORMAL,	// 贴花的法线贴图纹理槽，对应 API 属性 Decal.texture_normal，用于提供表面法线信息，从而影响光照和高光效果
		DECAL_TEXTURE_ORM,		// 贴花的遮蔽（Occlusion）/粗糙度（Roughness）/金属度（Metallic）合成纹理槽，对应 API 属性 Decal.texture_orm，常将 AO、Roughness、Metallic 分别存储在此纹理的不同通道中
		DECAL_TEXTURE_EMISSION,	// 贴花的自发光（Emission）纹理槽，对应 API 属性 Decal.texture_emission，用于在投影表面上额外添加发光效果 
		DECAL_TEXTURE_MAX
	};

	// 贴花资源创建
	virtual RID decal_create() = 0;
	// 设置贴花的尺寸
	virtual void decal_set_size(RID p_decal, const Vector3 &p_size) = 0;
	// 设置贴花的纹理，有多个不同类型的纹理需要设置
	virtual void decal_set_texture(RID p_decal, DecalTexture p_type, RID p_texture) = 0;
	// 设置贴花的自发光强度。p_energy 是发光强度的数值，数值越高，贴花的发光效果越明显。
	virtual void decal_set_emission_energy(RID p_decal, float p_energy) = 0;
	// 设置贴花的漫反射混合比例。p_mix 的数值范围通常是 0 到 1，0 表示完全透明，1 表示完全不透明。
	virtual void decal_set_albedo_mix(RID p_decal, float p_mix) = 0;
	// 设置贴花的颜色调节。p_modulate 是一个颜色对象，用于改变贴花的颜色。
	virtual void decal_set_modulate(RID p_decal, const Color &p_modulate) = 0;
	// 设置贴花的剔除层级。p_layers 是一个位掩码，用于指定哪些层级的物体会被贴花影响。
	virtual void decal_set_cull_mask(RID p_decal, uint32_t p_layers) = 0;
	// 设置贴花的距离淡化效果。p_enabled 启用或禁用淡化，p_begin 是开始淡化的距离，p_length 是淡化的长度。
	virtual void decal_set_distance_fade(RID p_decal, bool p_enabled, float p_begin, float p_length) = 0;
	// 设置贴花的垂直方向淡化。p_above 是上方淡化的强度，p_below 是下方淡化的强度。
	virtual void decal_set_fade(RID p_decal, float p_above, float p_below) = 0;
	// 设置贴花的法线淡化。p_fade 的数值范围通常是 0 到 1，数值越高，贴花在与表面法线角度较大的区域越不明显。
	virtual void decal_set_normal_fade(RID p_decal, float p_fade) = 0;

	// 贴花的纹理过滤方式，和上面投影灯的纹理过滤方式类似
	enum DecalFilter {
		DECAL_FILTER_NEAREST,
		DECAL_FILTER_LINEAR,
		DECAL_FILTER_NEAREST_MIPMAPS,
		DECAL_FILTER_LINEAR_MIPMAPS,
		DECAL_FILTER_NEAREST_MIPMAPS_ANISOTROPIC,
		DECAL_FILTER_LINEAR_MIPMAPS_ANISOTROPIC,
	};

	// 设置贴花的滤波方式
	virtual void decals_set_filter(DecalFilter p_quality) = 0;

	/* VOXEL GI API */

	// 创建体素GI资源
	virtual RID voxel_gi_create() = 0;

	// 体素GI分配数据内存
	virtual void voxel_gi_allocate_data(RID p_voxel_gi, const Transform3D &p_to_cell_xform, const AABB &p_aabb, const Vector3i &p_octree_size, const Vector<uint8_t> &p_octree_cells, const Vector<uint8_t> &p_data_cells, const Vector<uint8_t> &p_distance_field, const Vector<int> &p_level_counts) = 0;

	// 获取AABB包围盒
	virtual AABB voxel_gi_get_bounds(RID p_voxel_gi) const = 0;
	// 获取八叉树尺寸
	virtual Vector3i voxel_gi_get_octree_size(RID p_voxel_gi) const = 0;
	// 返回一个 PackedByteArray（底层 C++ 类型为 Vector<uint8_t>），表示体素八叉树（Octree）节点的原始字节序列。该结构编码了空间层次划分信息，可用于根据需要遍历或可视化体素树的占用情况和层级分布
	virtual Vector<uint8_t> voxel_gi_get_octree_cells(RID p_voxel_gi) const = 0;
	// 返回一个 PackedByteArray，包含每个体素单元存储的光照数据（如辐照度或辐射度）的压缩或编码值。这些数据是在 bake 过程中计算得到的，用于在运行时根据采样点索引检索对应的间接光照信息
	virtual Vector<uint8_t> voxel_gi_get_data_cells(RID p_voxel_gi) const = 0;
	// 返回一个 PackedByteArray，表示每个体素单元到场景几何体最近表面的距离场（Distance Field）数据。距离场信息可用于遮蔽计算、边界检测或更高级的基于距离的效果（如体积雾或软阴影近似）
	virtual Vector<uint8_t> voxel_gi_get_distance_field(RID p_voxel_gi) const = 0;
	// 返回一个 PackedInt32Array（底层 C++ 类型为 Vector<int>），表示八叉树各个层级（LOD）上的节点数量统计。通过该数组可以了解每一层中活跃节点数，用于性能分析、内存布局优化或调试八叉树构建质量
	virtual Vector<int> voxel_gi_get_level_counts(RID p_voxel_gi) const = 0;
	// 返回一个 Transform3D，用于将世界空间坐标转换到体素网格（cell）坐标系。调用者可以使用该变换矩阵将任意世界坐标点映射到对应的体素单元索引，从而在上述 PackedByteArray 中定位正确的数据位置
	virtual Transform3D voxel_gi_get_to_cell_xform(RID p_voxel_gi) const = 0;

	// 设置 VoxelGIData.dynamic_range，即间接光照中可表示的最大亮度。较高的动态范围允许更亮的间接光，但会降低精度并可能引入可见的色带效应
	virtual void voxel_gi_set_dynamic_range(RID p_voxel_gi, float p_range) = 0;
	// 设置光照在体素中传播时的能量衰减因子。较高的传播值会使间接光更加明亮和扩散，但可能导致画面变得过于平坦；启用双次反弹时，通常需要调低此值以抵消整体亮度增加
	virtual void voxel_gi_set_propagation(RID p_voxel_gi, float p_range) = 0;
	// 设置间接光照和发光材质的整体能量倍率。提高该值会使全局光照变亮；如果间接光过于平坦，可尝试同时降低传播并提高能量
	virtual void voxel_gi_set_energy(RID p_voxel_gi, float p_energy) = 0;
	// 告知渲染器在烘焙过程中使用的曝光归一化（Exposure Value），以便在运行时确保即便场景整体曝光变化，体素 GI 的亮度仍然保持一致。此值会按需在运行时进行调制
	virtual void voxel_gi_set_baked_exposure_normalization(RID p_voxel_gi, float p_baked_exposure) = 0;
	// 设置在运行时对体素查找的固定偏移量，以减少自遮挡（self-occlusion）伪影。适当的偏移有助于防止光照数据采样到自身表面
	virtual void voxel_gi_set_bias(RID p_voxel_gi, float p_bias) = 0;
	// 设置基于表面法线方向的偏移量（Normal Bias），同样用于缓解自反射伪影。较高的法线偏移可隐藏自反射，但可能增加光泄漏并让间接光显得更平坦 
	virtual void voxel_gi_set_normal_bias(RID p_voxel_gi, float p_range) = 0;
	// 启用“室内模式”后，不再考虑环境天空光对体素全局光照的贡献。适用于室内场景，可避免外部环境光通过体素边界泄漏进来
	virtual void voxel_gi_set_interior(RID p_voxel_gi, bool p_enable) = 0;
	// 启用双次反弹模式，使光照在首次反弹后再进行一次二次反弹，获得更真实的间接光效果并在反射中可见间接光；通常对性能影响极小
	virtual void voxel_gi_set_use_two_bounces(RID p_voxel_gi, bool p_enable) = 0;

	enum VoxelGIQuality {
		VOXEL_GI_QUALITY_LOW,
		VOXEL_GI_QUALITY_HIGH,
	};

	virtual void voxel_gi_set_quality(VoxelGIQuality) = 0;

	virtual void sdfgi_reset() = 0;

	/* LIGHTMAP */

	// 控制烘焙光照时生成的 shadowmask 纹理与实时阴影的混合方式
	enum ShadowmaskMode {
		SHADOWMASK_MODE_NONE,
		SHADOWMASK_MODE_REPLACE,
		SHADOWMASK_MODE_OVERLAY,
		SHADOWMASK_MODE_ONLY,
	};

	// 光照贴图资源创建
	virtual RID lightmap_create() = 0;

	// 将由 light RID 指定的一组贴图（通常是一个多层贴图数组）绑定到 lightmap GI 实例上，用以在渲染时进行光照采样；如果光照贴图在烘焙时启用了 Directional（方向性）模式，则参数 uses_sh 必须为 true，以告知渲染器使用球谐函数（Spherical Harmonics, SH）数据进行间接光照插值
	virtual void lightmap_set_textures(RID p_lightmap, RID p_light, bool p_uses_spherical_haromics) = 0;
	// 为 GI 探针体积设置轴对齐包围盒 (AABB)，定义探针在世界空间中的采样范围，框定光照探针数据的有效区域
	virtual void lightmap_set_probe_bounds(RID p_lightmap, const AABB &p_bounds) = 0;
	// 指定该探针体积是否作为“内部”空间使用（如室内光照采样）。当 interior=true 时，渲染器会以室内环境的采样方式处理探针；否则按户外环境处理
	virtual void lightmap_set_probe_interior(RID p_lightmap, bool p_interior) = 0;
	// 提交探针采样的完整数据
	virtual void lightmap_set_probe_capture_data(RID p_lightmap, const PackedVector3Array &p_points, const PackedColorArray &p_point_sh, const PackedInt32Array &p_tetrahedra, const PackedInt32Array &p_bsp_tree) = 0;
	// 将烘焙时所使用的曝光归一化系数 baked_exposure 通知渲染器，以便在运行时正确地对光照贴图颜色进行放缩，保持烘焙效果与场景亮度一致
	virtual void lightmap_set_baked_exposure_normalization(RID p_lightmap, float p_exposure) = 0;
	// 返回先前通过 lightmap_set_probe_capture_data 提交的采样点位置数组
	virtual PackedVector3Array lightmap_get_probe_capture_points(RID p_lightmap) const = 0;
	// 返回先前提交的每个采样点的球谐函数（SH）系数数组
	virtual PackedColorArray lightmap_get_probe_capture_sh(RID p_lightmap) const = 0;
	// 返回四面体网格的索引数组，用于光照插值拓扑结构
	virtual PackedInt32Array lightmap_get_probe_capture_tetrahedra(RID p_lightmap) const = 0;
	// 返回用于空间加速查询的 BSP 树索引数组
	virtual PackedInt32Array lightmap_get_probe_capture_bsp_tree(RID p_lightmap) const = 0;

	// 设置探针数据（Probe capture）在运行时更新的速率或权重，用于控制动态场景中探针光照数据的更新频率与插值响应速度
	virtual void lightmap_set_probe_capture_update_speed(float p_speed) = 0;
	// 切换是否对光照贴图使用双三次 (bicubic) 滤波。在采样时启用该滤波可使光照过渡更加平滑，但会带来额外的性能开销
	virtual void lightmaps_set_bicubic_filter(bool p_enable) = 0;

	// 为 lightmap 设置阴影蒙版贴图资源，该资源由 shadow RID 指向的贴图数组提供，主要用于混合光照贴图和实时阴影
	virtual void lightmap_set_shadowmask_textures(RID p_lightmap, RID p_shadow) = 0;
	// 查询当前 lightmap 使用的阴影蒙版模式
	virtual ShadowmaskMode lightmap_get_shadowmask_mode(RID p_lightmap) = 0;
	// 设置 lightmap 的阴影蒙版模式为上述枚举值之一，以切换对应的混合策略
	virtual void lightmap_set_shadowmask_mode(RID p_lightmap, ShadowmaskMode p_mode) = 0;

	/* PARTICLES API */
	// 粒子API

	// 创建粒子资源
	virtual RID particles_create() = 0;

	// 粒子模式：2D/3D
	enum ParticlesMode {
		PARTICLES_MODE_2D,
		PARTICLES_MODE_3D
	};
	virtual void particles_set_mode(RID p_particles, ParticlesMode p_mode) = 0;

	// 开启或停止粒子发射
	virtual void particles_set_emitting(RID p_particles, bool p_enable) = 0;
	virtual bool particles_get_emitting(RID p_particles) = 0;
	// 粒子发射数量
	virtual void particles_set_amount(RID p_particles, int p_amount) = 0;
	// 粒子发射强度
	virtual void particles_set_amount_ratio(RID p_particles, float p_amount_ratio) = 0;
	// 粒子存活时间
	virtual void particles_set_lifetime(RID p_particles, double p_lifetime) = 0;
	// 是否单次发射
	virtual void particles_set_one_shot(RID p_particles, bool p_one_shot) = 0;
	// 预运行时间，用于在首次渲染前填充粒子效果
	virtual void particles_set_pre_process_time(RID p_particles, double p_time) = 0;
	// 设定输出爆发比例
	virtual void particles_set_explosiveness_ratio(RID p_particles, float p_ratio) = 0;
	// 设定随机性
	virtual void particles_set_randomness_ratio(RID p_particles, float p_ratio) = 0;
	// 手动设定粒子系统的包围盒，用于可视化剔除
	virtual void particles_set_custom_aabb(RID p_particles, const AABB &p_aabb) = 0;
	// 设置速度比例
	virtual void particles_set_speed_scale(RID p_particles, double p_scale) = 0;
	// 是否使用本地坐标（父节点移动时，粒子是否跟随）
	virtual void particles_set_use_local_coordinates(RID p_particles, bool p_enable) = 0;
	// 设置粒子的材质
	virtual void particles_set_process_material(RID p_particles, RID p_material) = 0;
	// 设置固定频率
	virtual void particles_set_fixed_fps(RID p_particles, int p_fps) = 0;
	// 设置是否插值
	virtual void particles_set_interpolate(RID p_particles, bool p_enable) = 0;
	// 使用分数时间余量计算，平滑粒子动画
	virtual void particles_set_fractional_delta(RID p_particles, bool p_enable) = 0;
	// 设置碰撞检测的基本粒子尺寸
	virtual void particles_set_collision_base_size(RID p_particles, float p_size) = 0;

	// 定义每个粒子在世界中的朝向方式，影响渲染时粒子的对齐行为
	enum ParticlesTransformAlign {
		PARTICLES_TRANSFORM_ALIGN_DISABLED,			// 关闭对齐，粒子使用自身定义的变换，不主动面向摄像机或移动方向
		PARTICLES_TRANSFORM_ALIGN_Z_BILLBOARD,		// 粒子的 Z 轴始终面向摄像机，类似于传统的 billboard 效果，这保证了粒子正面总是朝向玩家视角。
		PARTICLES_TRANSFORM_ALIGN_Y_TO_VELOCITY,	// 粒子 Y 轴（通常代表“上”方向）与它的速度方向对齐，适用于模拟诸如射击弹道、拖尾效果等效果，使得粒子“指向”它正在移动的方向
		PARTICLES_TRANSFORM_ALIGN_Z_BILLBOARD_Y_TO_VELOCITY,	// 同时应用以上两者：粒子的 Z 轴对准摄像机，Y 轴对准速度方向，可用于制作既面向视角又方向感强的粒子效果（如飞舞的火焰或烟雾流动效果）
	};

	// 设置是否面向相机，以及移动的方向
	virtual void particles_set_transform_align(RID p_particles, ParticlesTransformAlign p_transform_align) = 0;

	// 开启或关闭粒子的尾迹效果（trail）。启用后，粒子将留下一个持续 p_length_sec（秒）的轨迹，使其运动轨迹可视化
	virtual void particles_set_trails(RID p_particles, bool p_enable, float p_length_sec) = 0;
	// 指定尾迹的绑定姿态（bind poses），通常用于为尾迹设置初始变换，确保尾迹与粒子的形态或骨骼结构保持一致
	virtual void particles_set_trail_bind_poses(RID p_particles, const Vector<Transform3D> &p_bind_poses) = 0;

	// 当粒子停止发射且已进入不活动状态时返回true
	virtual bool particles_is_inactive(RID p_particles) = 0;
	// 将该粒子系统标记为需要更新（处理），会在下一帧或下一次可见性剔除时进行仿真。等同于 “手动触发” 粒子系统更新
	virtual void particles_request_process(RID p_particles) = 0;
	// 重置粒子系统，使其在下一次更新时重新开始发射所有粒子，相当于 GPUParticles3D.restart 的功能。
	virtual void particles_restart(RID p_particles) = 0;

	// 设置另一个粒子系统作为当前粒子的子发射器（subemitter）
	// 当父粒子触发条件（如消亡）时，会触发子发射器发出新的粒子效果，用于实现如爆炸、碎裂等连锁特效。
	virtual void particles_set_subemitter(RID p_particles, RID p_subemitter_particles) = 0;

	// 单个粒子时，哪些初始属性
	enum ParticlesEmitFlags {
		PARTICLES_EMIT_FLAG_POSITION = 1,			// 位置
		PARTICLES_EMIT_FLAG_ROTATION_SCALE = 2,		// 旋转、缩放
		PARTICLES_EMIT_FLAG_VELOCITY = 4,			// 速度
		PARTICLES_EMIT_FLAG_COLOR = 8,				// 颜色
		PARTICLES_EMIT_FLAG_CUSTOM = 16				// 自定义
	};

	// 设置发射的粒子属性
	virtual void particles_emit(RID p_particles, const Transform3D &p_transform, const Vector3 &p_velocity, const Color &p_color, const Color &p_custom, uint32_t p_emit_flags) = 0;

	// 用来决定粒子在渲染时的 绘制顺序
	enum ParticlesDrawOrder {
		PARTICLES_DRAW_ORDER_INDEX,				// 按 发射顺序 绘制
		PARTICLES_DRAW_ORDER_LIFETIME,			// 按 剩余存活时间 升序，剩余时间越少的粒子越靠前绘制
		PARTICLES_DRAW_ORDER_REVERSE_LIFETIME,	// 按 剩余存活时间 降序：剩余时间多的粒子最前
		PARTICLES_DRAW_ORDER_VIEW_DEPTH,		// 按 摄像机深度 绘制：离摄像机近的粒子覆盖远的粒子
	};
	// 设置绘制顺序
	virtual void particles_set_draw_order(RID p_particles, ParticlesDrawOrder p_order) = 0;

	// 设置粒子系统的 渲染通道（draw passes）数量。这相当于在 GPUParticles3D.draw_passes 中
	// 配置通道数量：一个粒子可以被绘制若干次（使用不同网格），实现复杂效果（如多层材质）
	virtual void particles_set_draw_passes(RID p_particles, int p_count) = 0;
	// 为对应的第 p_pass 渲染通道指定 网格（Mesh）资源。类似于在 GPUParticles3D.draw_pass_1/2/3… 设置的 mesh，用于自定义粒子的形态。
	virtual void particles_set_draw_pass_mesh(RID p_particles, int p_pass, RID p_mesh) = 0;

	// 获取当前粒子系统的 轴对齐包围盒（AABB），包含了所有活跃粒子的位置范围。可用于调试、可见性检测或动态调整场景布局
	virtual AABB particles_get_current_aabb(RID p_particles) = 0;

	// 设置粒子 发射器变换，即定义粒子在初始时的位置/旋转/缩放。仅应用于 2D 粒子系统；在 3D 中发射器的变换会自动继承父节点的变换
	virtual void particles_set_emission_transform(RID p_particles, const Transform3D &p_transform) = 0; // This is only used for 2D, in 3D it's automatic.
	// 指定发射器本身的 运动速度，用以计算粒子的初始速度
	virtual void particles_set_emitter_velocity(RID p_particles, const Vector3 &p_velocity) = 0;
	// 设置一种 生命周期插值逻辑，从粒子生命周期开始时的状态平滑过渡到结束状态，
	// 减少视觉突变，提升平滑度。等效于 GPUParticles3D.interpolate_to_end 或 interp_to_end 属性
	virtual void particles_set_interp_to_end(RID p_particles, float p_interp) = 0;

	/* PARTICLES COLLISION API */
	// 粒子碰撞API

	// 粒子碰撞资源创建
	virtual RID particles_collision_create() = 0;

	// 粒子与环境或自定义场景的碰撞/吸引行为模式
	enum ParticlesCollisionType {
		PARTICLES_COLLISION_TYPE_SPHERE_ATTRACT,		// 球形吸引
		PARTICLES_COLLISION_TYPE_BOX_ATTRACT,			// 盒形吸引
		PARTICLES_COLLISION_TYPE_VECTOR_FIELD_ATTRACT,	// 矢量场吸引
		PARTICLES_COLLISION_TYPE_SPHERE_COLLIDE,		// 球形碰撞
		PARTICLES_COLLISION_TYPE_BOX_COLLIDE,			// 盒形碰撞
		PARTICLES_COLLISION_TYPE_SDF_COLLIDE,			// 距离场配装
		PARTICLES_COLLISION_TYPE_HEIGHTFIELD_COLLIDE,	// 高度场碰撞
	};

	// 指定碰撞或吸引器类型
	virtual void particles_collision_set_collision_type(RID p_particles_collision, ParticlesCollisionType p_type) = 0;
	// 设置碰撞/吸引器的 剔除层 (cull mask)，用于指定哪些场景物体会被此碰撞系统影响
	virtual void particles_collision_set_cull_mask(RID p_particles_collision, uint32_t p_cull_mask) = 0;
	// 为球状碰撞器或球状吸引器设置半径，控制影响范围
	virtual void particles_collision_set_sphere_radius(RID p_particles_collision, real_t p_radius) = 0; // For spheres.
	// 设定盒状体的半尺寸（extents），用于吸引或碰撞盒区域。
	virtual void particles_collision_set_box_extents(RID p_particles_collision, const Vector3 &p_extents) = 0; // For non-spheres.
	// 设置吸引力强度，仅在“吸引器模式”下生效
	virtual void particles_collision_set_attractor_strength(RID p_particles_collision, real_t p_strength) = 0;
	// 设定粒子被吸引时的方向性程度（directionality）
	virtual void particles_collision_set_attractor_directionality(RID p_particles_collision, real_t p_directionality) = 0;
	// 控制吸引力随距离的衰减曲线。值可用于调整吸引力随距离变化的衰减形状。
	virtual void particles_collision_set_attractor_attenuation(RID p_particles_collision, real_t p_curve) = 0;
	// 为基于 SDF 或矢量场的碰撞/吸引器指定纹理资源
	virtual void particles_collision_set_field_texture(RID p_particles_collision, RID p_texture) = 0; // For SDF and vector field, heightfield is dynamic.

	// 在 heightfield（高度图）模式下，更新或同步地形数据。适用于动态高度场的碰撞检测
	virtual void particles_collision_height_field_update(RID p_particles_collision) = 0; // For SDF and vector field.

	//  控制 GPU 粒子高度场碰撞系统的高度图网格解析度，也就是用于
	//  GPUParticlesCollisionHeightField3D（或通过 RenderingServer 接口）时，
	// 指定用于生成碰撞高度图的分辨率大小。
	// 用于粒子和地形的互动。
	enum ParticlesCollisionHeightfieldResolution { // Longest axis resolution.
		PARTICLES_COLLISION_HEIGHTFIELD_RESOLUTION_256,
		PARTICLES_COLLISION_HEIGHTFIELD_RESOLUTION_512,
		PARTICLES_COLLISION_HEIGHTFIELD_RESOLUTION_1024,
		PARTICLES_COLLISION_HEIGHTFIELD_RESOLUTION_2048,
		PARTICLES_COLLISION_HEIGHTFIELD_RESOLUTION_4096,
		PARTICLES_COLLISION_HEIGHTFIELD_RESOLUTION_8192,
		PARTICLES_COLLISION_HEIGHTFIELD_RESOLUTION_MAX,
	};

	// 设置高度图分辨率
	virtual void particles_collision_set_height_field_resolution(RID p_particles_collision, ParticlesCollisionHeightfieldResolution p_resolution) = 0; // For SDF and vector field.

	/* FOG VOLUME API */
	// 雾体积API

	// 雾体积创建
	virtual RID fog_volume_create() = 0;

	// 雾体积的形状枚举
	enum FogVolumeShape {
		FOG_VOLUME_SHAPE_ELLIPSOID,		// 雾体积将呈现为一个椭球形（类似拉伸的球体）
		FOG_VOLUME_SHAPE_CONE,			// 雾体积为一个向上指向的圆锥体。
		FOG_VOLUME_SHAPE_CYLINDER,		// 呈现为直立的圆柱体
		FOG_VOLUME_SHAPE_BOX,			// 体积雾呈轴对齐的长方体
		FOG_VOLUME_SHAPE_WORLD,			// 没有特定体积形状，雾效果覆盖整个世界，并且不会被边界裁剪掉。适用于创建环境级的全域雾
		FOG_VOLUME_SHAPE_MAX,
	};

	// 设置雾形状
	virtual void fog_volume_set_shape(RID p_fog_volume, FogVolumeShape p_shape) = 0;
	// 设置雾尺寸
	virtual void fog_volume_set_size(RID p_fog_volume, const Vector3 &p_size) = 0;
	// 设置雾材质
	virtual void fog_volume_set_material(RID p_fog_volume, RID p_material) = 0;

	/* VISIBILITY NOTIFIER API */
	// 可见性通知器API
	// 这是一块区域，开发者自己设置的区域，目的是为了降低消耗。当这块区域进入相机范围内，被相机“看见”之后，
	// 区域内的物体才开始刷新，但是当离开之后，这区域内的物体就不刷新了。

	// 创建可见性通知器
	virtual RID visibility_notifier_create() = 0;
	// 设置可见性通知器的aabb
	virtual void visibility_notifier_set_aabb(RID p_notifier, const AABB &p_aabb) = 0;
	// 设置可见性通知器的回调，包括进入和退出时候的回调
	virtual void visibility_notifier_set_callbacks(RID p_notifier, const Callable &p_enter_callbable, const Callable &p_exit_callable) = 0;

	/* OCCLUDER API */
	// 遮挡器API

	// 创建遮挡器资源
	virtual RID occluder_create() = 0;
	// 设置遮挡器的形状
	virtual void occluder_set_mesh(RID p_occluder, const PackedVector3Array &p_vertices, const PackedInt32Array &p_indices) = 0;

	/* CAMERA API */
	// 相机API

	// 相机创建
	virtual RID camera_create() = 0;
	// 相机设置为透视相机
	virtual void camera_set_perspective(RID p_camera, float p_fovy_degrees, float p_z_near, float p_z_far) = 0;
	// 相机设置为正交相机
	virtual void camera_set_orthogonal(RID p_camera, float p_size, float p_z_near, float p_z_far) = 0;
	// 相机设置视锥体
	virtual void camera_set_frustum(RID p_camera, float p_size, Vector2 p_offset, float p_z_near, float p_z_far) = 0;
	// 设置相机的变换
	virtual void camera_set_transform(RID p_camera, const Transform3D &p_transform) = 0;
	// 指定该相机应渲染哪些 3D 图层（Layer）。所有处于 p_layers 指定二进制掩码中的图层才会被该相机可见并绘制，其余图层将被剔除。
	virtual void camera_set_cull_mask(RID p_camera, uint32_t p_layers) = 0;
	// 将一个环境（Environment）资源绑定到指定相机上，用于控制天空盒、环境光、后期效果等全局渲染设置。
	virtual void camera_set_environment(RID p_camera, RID p_env) = 0;
	// 将先前通过 camera_attributes_create() 创建的相机属性资源（CameraAttributes）绑定到该相机上，用于控制景深（DOF）、自动曝光、曝光覆写等高级渲染属性。
	virtual void camera_set_camera_attributes(RID p_camera, RID p_camera_attributes) = 0;
	// 为相机指定一个后处理合成器（Compositor）资源，用于自定义渲染管线中的后期处理效果（如色调映射、泛光、色彩校正等）。
	virtual void camera_set_compositor(RID p_camera, RID p_compositor) = 0;
	// 控制在屏幕宽高比与目标视口比例不一致时的缩放策略
	virtual void camera_set_use_vertical_aspect(RID p_camera, bool p_enable) = 0;

	/* VIEWPORT API */
	// 视口API

	// 画布纹理滤波方式
	enum CanvasItemTextureFilter {
		CANVAS_ITEM_TEXTURE_FILTER_DEFAULT, // Uses canvas item setting for draw command, uses global setting for canvas item.
		CANVAS_ITEM_TEXTURE_FILTER_NEAREST,
		CANVAS_ITEM_TEXTURE_FILTER_LINEAR,
		CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS,
		CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS,
		CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS_ANISOTROPIC,
		CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC,
		CANVAS_ITEM_TEXTURE_FILTER_MAX
	};

	// 画布纹理重复方式
	enum CanvasItemTextureRepeat {
		CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT, // Uses canvas item setting for draw command, uses global setting for canvas item.
		CANVAS_ITEM_TEXTURE_REPEAT_DISABLED,
		CANVAS_ITEM_TEXTURE_REPEAT_ENABLED,
		CANVAS_ITEM_TEXTURE_REPEAT_MIRROR,
		CANVAS_ITEM_TEXTURE_REPEAT_MAX,
	};

	// 视口创建
	virtual RID viewport_create() = 0;

	// 视口缩放3D模式
	enum ViewportScaling3DMode {
		VIEWPORT_SCALING_3D_MODE_BILINEAR,		// 使用双线性过滤进行缩放。
		VIEWPORT_SCALING_3D_MODE_FSR,			// 使用 AMD FidelityFX Super Resolution 1.0 进行上采样
		VIEWPORT_SCALING_3D_MODE_FSR2,			// 使用 AMD FidelityFX Super Resolution 2.2 进行上采样
		VIEWPORT_SCALING_3D_MODE_METALFX_SPATIAL,		// 使用 Apple MetalFX 空域上采样器，仅在 macOS/iOS 平台的 Metal 驱动下可用。
		VIEWPORT_SCALING_3D_MODE_METALFX_TEMPORAL,		// 使用 Apple MetalFX 时域上采样器，同样仅限 Metal 驱动平台。小于 1.0 时放大渲染
		VIEWPORT_SCALING_3D_MODE_MAX,
		VIEWPORT_SCALING_3D_MODE_OFF = 255, // for internal use only
	};

	// 各向异性滤波的级别
	enum ViewportAnisotropicFiltering {
		VIEWPORT_ANISOTROPY_DISABLED,
		VIEWPORT_ANISOTROPY_2X,
		VIEWPORT_ANISOTROPY_4X,
		VIEWPORT_ANISOTROPY_8X,
		VIEWPORT_ANISOTROPY_16X,
		VIEWPORT_ANISOTROPY_MAX
	};

	// 诉渲染管线所选的 3D 分辨率缩放模式属于哪种“域”（无缩放、空间或时域）
	enum ViewportScaling3DType {
		VIEWPORT_SCALING_3D_TYPE_NONE,
		VIEWPORT_SCALING_3D_TYPE_TEMPORAL,
		VIEWPORT_SCALING_3D_TYPE_SPATIAL,
		VIEWPORT_SCALING_3D_TYPE_MAX,
	};

	// 区分视口的缩放是时域还是空域的类型
	_ALWAYS_INLINE_ static ViewportScaling3DType scaling_3d_mode_type(ViewportScaling3DMode p_mode) {
		if (p_mode == VIEWPORT_SCALING_3D_MODE_BILINEAR || p_mode == VIEWPORT_SCALING_3D_MODE_FSR || p_mode == VIEWPORT_SCALING_3D_MODE_METALFX_SPATIAL) {
			return VIEWPORT_SCALING_3D_TYPE_SPATIAL;
		} else if (p_mode == VIEWPORT_SCALING_3D_MODE_FSR2 || p_mode == VIEWPORT_SCALING_3D_MODE_METALFX_TEMPORAL) {
			return VIEWPORT_SCALING_3D_TYPE_TEMPORAL;
		}
		return VIEWPORT_SCALING_3D_TYPE_NONE;
	}

	// 设置是否使用xr
	virtual void viewport_set_use_xr(RID p_viewport, bool p_use_xr) = 0;
	// 设置视口大小
	virtual void viewport_set_size(RID p_viewport, int p_width, int p_height) = 0;
	// 设置视口是否激活
	virtual void viewport_set_active(RID p_viewport, bool p_active) = 0;
	// 设置父视口
	virtual void viewport_set_parent_viewport(RID p_viewport, RID p_parent_viewport) = 0;
	// 设置视口画布的剔除掩码
	virtual void viewport_set_canvas_cull_mask(RID p_viewport, uint32_t p_canvas_cull_mask) = 0;

	// 将视口附加到屏幕上
	virtual void viewport_attach_to_screen(RID p_viewport, const Rect2 &p_rect = Rect2(), DisplayServer::WindowID p_screen = DisplayServer::MAIN_WINDOW_ID) = 0;
	// 设置视口直接绘制到屏幕上
	virtual void viewport_set_render_direct_to_screen(RID p_viewport, bool p_enable) = 0;

	// 设置3D缩放模式
	virtual void viewport_set_scaling_3d_mode(RID p_viewport, ViewportScaling3DMode p_scaling_3d_mode) = 0;
	// 设置3D缩放比例
	virtual void viewport_set_scaling_3d_scale(RID p_viewport, float p_scaling_3d_scale) = 0;
	// 设置fsr的锐度
	virtual void viewport_set_fsr_sharpness(RID p_viewport, float p_fsr_sharpness) = 0;
	// 设置视口纹理的mipmap偏移
	virtual void viewport_set_texture_mipmap_bias(RID p_viewport, float p_texture_mipmap_bias) = 0;
	// 设置各向异性的滤波等级
	virtual void viewport_set_anisotropic_filtering_level(RID p_viewport, ViewportAnisotropicFiltering p_anisotropic_filtering_level) = 0;

	// 视口刷新模式：禁止刷新、刷新一次、可见的时候刷新、父节点可见的时候刷新、总是刷新
	enum ViewportUpdateMode {
		VIEWPORT_UPDATE_DISABLED,
		VIEWPORT_UPDATE_ONCE, // Then goes to disabled, must be manually updated.
		VIEWPORT_UPDATE_WHEN_VISIBLE, // Default
		VIEWPORT_UPDATE_WHEN_PARENT_VISIBLE,
		VIEWPORT_UPDATE_ALWAYS
	};

	// 设置/获取视口刷新模式
	virtual void viewport_set_update_mode(RID p_viewport, ViewportUpdateMode p_mode) = 0;
	virtual ViewportUpdateMode viewport_get_update_mode(RID p_viewport) const = 0;

	// 视口清除模式
	enum ViewportClearMode {
		VIEWPORT_CLEAR_ALWAYS,		// 总是清除
		VIEWPORT_CLEAR_NEVER,		// 从不清除
		VIEWPORT_CLEAR_ONLY_NEXT_FRAME		// 视口会在紧接着的下一帧被清除一次，之后自动切换回 NEVER 模式（即后续帧不再清除）
	};

	// 设置视口的清除模式
	virtual void viewport_set_clear_mode(RID p_viewport, ViewportClearMode p_clear_mode) = 0;

	// 获取视口的渲染目标
	virtual RID viewport_get_render_target(RID p_viewport) const = 0;
	// 获取视口的纹理对象
	virtual RID viewport_get_texture(RID p_viewport) const = 0;

	// 视口的环境效果模式
	enum ViewportEnvironmentMode {
		VIEWPORT_ENVIRONMENT_DISABLED,		// 禁用环境效果
		VIEWPORT_ENVIRONMENT_ENABLED,		// 启用环境效果
		VIEWPORT_ENVIRONMENT_INHERIT,		// 环境效果跟着父节点
		VIEWPORT_ENVIRONMENT_MAX,
	};

	// 设置环境模式
	virtual void viewport_set_environment_mode(RID p_viewport, ViewportEnvironmentMode p_mode) = 0;
	// 设置禁用3D
	virtual void viewport_set_disable_3d(RID p_viewport, bool p_disable) = 0;
	// 设置禁用2D
	virtual void viewport_set_disable_2d(RID p_viewport, bool p_disable) = 0;

	// 将相机绑定到视口（决定渲染视角）
	virtual void viewport_attach_camera(RID p_viewport, RID p_camera) = 0;
	// 关联 3D 场景（包含世界环境、光照、几何体）
	virtual void viewport_set_scenario(RID p_viewport, RID p_scenario) = 0;
	// 添加/移除 2D 画布（用于 UI 或 2D 渲染）
	virtual void viewport_attach_canvas(RID p_viewport, RID p_canvas) = 0;
	virtual void viewport_remove_canvas(RID p_viewport, RID p_canvas) = 0;
	// 设置画布的全局变换矩阵
	virtual void viewport_set_canvas_transform(RID p_viewport, RID p_canvas, const Transform2D &p_offset) = 0;
	// 设置是否使用透明背景
	virtual void viewport_set_transparent_background(RID p_viewport, bool p_enabled) = 0;
	// 控制 2D 渲染是否使用 HDR（高动态范围）
	virtual void viewport_set_use_hdr_2d(RID p_viewport, bool p_use_hdr) = 0;
	virtual bool viewport_is_using_hdr_2d(RID p_viewport) const = 0;
	// 变换后位置对齐到像素
	virtual void viewport_set_snap_2d_transforms_to_pixel(RID p_viewport, bool p_enabled) = 0;
	// 原始顶点数据对齐到像素
	virtual void viewport_set_snap_2d_vertices_to_pixel(RID p_viewport, bool p_enabled) = 0;

	// 该视口中所有 CanvasItem 使用的默认纹理过滤模式
	virtual void viewport_set_default_canvas_item_texture_filter(RID p_viewport, CanvasItemTextureFilter p_filter) = 0;
	// 设置该视口中所有 CanvasItem 使用的默认纹理重复模式
	virtual void viewport_set_default_canvas_item_texture_repeat(RID p_viewport, CanvasItemTextureRepeat p_repeat) = 0;

	// 为整个视口的所有 2D 画布元素应用一个全局仿射变换，常用于实现屏幕缩放、旋转或平移等效果
	virtual void viewport_set_global_canvas_transform(RID p_viewport, const Transform2D &p_transform) = 0;
	// 设置指定画布在视口内的层叠层级（layer）及该层内的子层叠顺序（sublayer）
	virtual void viewport_set_canvas_stacking(RID p_viewport, RID p_canvas, int p_layer, int p_sublayer) = 0;

	// 该枚举定义了 SDF 在视口边缘之外的额外“扩展”百分比，用于避免当场景中的碰撞体或遮挡体紧贴视口边缘时 SDF 被意外截断
	enum ViewportSDFOversize {
		VIEWPORT_SDF_OVERSIZE_100_PERCENT,
		VIEWPORT_SDF_OVERSIZE_120_PERCENT,
		VIEWPORT_SDF_OVERSIZE_150_PERCENT,
		VIEWPORT_SDF_OVERSIZE_200_PERCENT,
		VIEWPORT_SDF_OVERSIZE_MAX
	};

	// 该枚举控制 SDF 纹理相对于视口的渲染分辨率，可用于降低 SDF 的像素密度，从而减少 GPU 负载。
	enum ViewportSDFScale {
		VIEWPORT_SDF_SCALE_100_PERCENT,
		VIEWPORT_SDF_SCALE_50_PERCENT,
		VIEWPORT_SDF_SCALE_25_PERCENT,
		VIEWPORT_SDF_SCALE_MAX
	};

	virtual void viewport_set_sdf_oversize_and_scale(RID p_viewport, ViewportSDFOversize p_oversize, ViewportSDFScale p_scale) = 0;

	// 设置指定视口（viewport）中全向灯和聚光灯阴影图集的贴图大小，并可选地使用 16 位深度贴图以降低显存占用和提高性能。
	virtual void viewport_set_positional_shadow_atlas_size(RID p_viewport, int p_size, bool p_16_bits = true) = 0;
	// 设置指定视口阴影图集某象限（quadrant）的细分数，用于控制在该象限中为全向灯和聚光灯生成更多或更少的阴影贴图页。细分数越高，可支持的光源数或阴影质量越高，但会增加内存和渲染开销。
	virtual void viewport_set_positional_shadow_atlas_quadrant_subdivision(RID p_viewport, int p_quadrant, int p_subdiv) = 0;

	// 视口的MSAA控制
	enum ViewportMSAA {
		VIEWPORT_MSAA_DISABLED,
		VIEWPORT_MSAA_2X,
		VIEWPORT_MSAA_4X,
		VIEWPORT_MSAA_8X,
		VIEWPORT_MSAA_MAX,
	};

	// 设置2D/3DmsAA的精度
	virtual void viewport_set_msaa_3d(RID p_viewport, ViewportMSAA p_msaa) = 0;
	virtual void viewport_set_msaa_2d(RID p_viewport, ViewportMSAA p_msaa) = 0;

	// ssaa的方式
	enum ViewportScreenSpaceAA {
		VIEWPORT_SCREEN_SPACE_AA_DISABLED,		// 禁用ssaa
		VIEWPORT_SCREEN_SPACE_AA_FXAA,			// 使用fxaa
		VIEWPORT_SCREEN_SPACE_AA_MAX,
	};

	// 设置ssaa的方式
	virtual void viewport_set_screen_space_aa(RID p_viewport, ViewportScreenSpaceAA p_mode) = 0;

	// 设置是否使用taa
	virtual void viewport_set_use_taa(RID p_viewport, bool p_use_taa) = 0;

	//  在指定的 Viewport 上开启或关闭“去条纹”（Debanding）后处理滤波器，用以减少色带（banding）现象，提高渐变区域的视觉质量
	virtual void viewport_set_use_debanding(RID p_viewport, bool p_use_debanding) = 0;

	// 控制指定 Viewport 是否 强制 生成 运动向量（motion vectors）
	virtual void viewport_set_force_motion_vectors(RID p_viewport, bool p_force_motion_vectors) = 0;

	// 设置网格lod的阈值
	virtual void viewport_set_mesh_lod_threshold(RID p_viewport, float p_pixels) = 0;

	// 按需为指定的 Viewport 开启或关闭遮挡剔除，相当于运行时修改 rendering/occlusion_culling/use_occlusion_culling 设置
	virtual void viewport_set_use_occlusion_culling(RID p_viewport, bool p_use_occlusion_culling) = 0;
	// 全局设置每个 CPU 线程在构建或更新遮挡剔除 BVH 时要投射的射线数量，对应于 rendering/occlusion_culling/occlusion_rays_per_thread，该值越大提高剔除精度，但也会成比例增加 CPU 开销
	virtual void viewport_set_occlusion_rays_per_thread(int p_rays_per_thread) = 0;

	// 遮挡剔除构建质量
	enum ViewportOcclusionCullingBuildQuality {
		VIEWPORT_OCCLUSION_BUILD_QUALITY_LOW = 0,
		VIEWPORT_OCCLUSION_BUILD_QUALITY_MEDIUM = 1,
		VIEWPORT_OCCLUSION_BUILD_QUALITY_HIGH = 2,
	};

	// 设置遮挡剔除的构建质量
	virtual void viewport_set_occlusion_culling_build_quality(ViewportOcclusionCullingBuildQuality p_quality) = 0;


	// 用于在运行时获取指定视口（Viewport）每帧的渲染统计数据。通过 ViewportRenderInfo 枚举选择要查询的统计项目（对象数、图元数、Draw Call 数等），通过 ViewportRenderInfoType 枚举选择要查询的渲染通道（可见、阴影或 Canvas）
	
	// 可查询的渲染统计类型
	enum ViewportRenderInfo {
		VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME,		// 本帧中绘制的可见对象（Object）总数
		VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME,	// 本帧中绘制的图元（点、线、三角形）总数
		VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME,	// 本帧中执行的 Draw Call（绘制调用）总数
		VIEWPORT_RENDER_INFO_MAX,
	};

	// 指定要查询的渲染通道
	enum ViewportRenderInfoType {
		VIEWPORT_RENDER_INFO_TYPE_VISIBLE,		// 可见渲染通道（不包含阴影）的统计数据
		VIEWPORT_RENDER_INFO_TYPE_SHADOW,		//  阴影渲染通道的统计数据；由于阴影贴图可能会多次渲染，统计值往往高于可见通道
		VIEWPORT_RENDER_INFO_TYPE_CANVAS,		// Canvas（2D）渲染通道的统计数据
		VIEWPORT_RENDER_INFO_TYPE_MAX
	};

	// 获取数据
	virtual int viewport_get_render_info(RID p_viewport, ViewportRenderInfoType p_type, ViewportRenderInfo p_info) = 0;

	// 开启各种调试可视化模式
	enum ViewportDebugDraw {
		VIEWPORT_DEBUG_DRAW_DISABLED,		// 禁用调试绘制，恢复正常渲染
		VIEWPORT_DEBUG_DRAW_UNSHADED,		// 仅显示物体的基础色，不应用任何光照信息
		VIEWPORT_DEBUG_DRAW_LIGHTING,		// 仅显示光照对场景的影响（不显示材质本身），便于调试光照分布。
		VIEWPORT_DEBUG_DRAW_OVERDRAW,		// 使用半透明叠加显示过度绘制区域，区域越亮代表像素被重复绘制次数越多。
		VIEWPORT_DEBUG_DRAW_WIREFRAME,		// 线框模式渲染所有几何体，可视化网格拓扑。
		VIEWPORT_DEBUG_DRAW_NORMAL_BUFFER,				// 绘制法线缓冲，查看每像素法线方向，便于调试法线贴图及后处理。
		VIEWPORT_DEBUG_DRAW_VOXEL_GI_ALBEDO,			// 仅显示体素全局光照（VoxelGI）的反照率（Albedo）通道。
		VIEWPORT_DEBUG_DRAW_VOXEL_GI_LIGHTING,			// 仅显示体素全局光照的光照通道。
		VIEWPORT_DEBUG_DRAW_VOXEL_GI_EMISSION,			// 仅显示体素全局光照的自发光通道。
		VIEWPORT_DEBUG_DRAW_SHADOW_ATLAS,				// 在左上象限绘制全向光 (OmniLight3D) 和聚光灯 (SpotLight3D) 的阴影图集。
		VIEWPORT_DEBUG_DRAW_DIRECTIONAL_SHADOW_ATLAS,	// 在左上象限绘制定向光阴影图集，并叠加 PSSM 切片可视化。
		VIEWPORT_DEBUG_DRAW_SCENE_LUMINANCE,			// 绘制自动曝光计算的场景亮度贴图（1×1 纹理）。
		VIEWPORT_DEBUG_DRAW_SSAO,						// 绘制屏幕空间环境光遮蔽（SSAO）贴图，需在 WorldEnvironment.ssao_enabled 打开时生效。
		VIEWPORT_DEBUG_DRAW_SSIL,						// 绘制屏幕空间间接光照（SSIL）贴图，需在 WorldEnvironment.ssil_enabled 打开时生效。
		VIEWPORT_DEBUG_DRAW_PSSM_SPLITS,				// 为各级定向阴影切片着色（红、绿、蓝、黄），可视化级联范围。
		VIEWPORT_DEBUG_DRAW_DECAL_ATLAS,				// 绘制贴花图集，显示所有 Decal 资源纹理。
		VIEWPORT_DEBUG_DRAW_SDFGI,						// 绘制 SDFGI（Signed Distance Field GI）级联数据结构。
		VIEWPORT_DEBUG_DRAW_SDFGI_PROBES,				// 绘制 SDFGI 探针数据，查看探针位置及影响范围。
		VIEWPORT_DEBUG_DRAW_GI_BUFFER,					// 绘制全局光照缓存（VoxelGI 或 SDFGI）。
		VIEWPORT_DEBUG_DRAW_DISABLE_LOD,				// 禁用所有模型的 LOD，强制以最高细节渲染，便于对比性能差异。
		VIEWPORT_DEBUG_DRAW_CLUSTER_OMNI_LIGHTS,		// 绘制全向光聚类（Forward+）结果，显示屏幕空间光源分布。
		VIEWPORT_DEBUG_DRAW_CLUSTER_SPOT_LIGHTS,		// 绘制聚光灯聚类结果。
		VIEWPORT_DEBUG_DRAW_CLUSTER_DECALS,				// 绘制贴花聚类结果。
		VIEWPORT_DEBUG_DRAW_CLUSTER_REFLECTION_PROBES,	// 绘制反射探针聚类结果。
		VIEWPORT_DEBUG_DRAW_OCCLUDERS,					// 绘制 CPU 生成的遮挡剔除缓冲，显示遮挡体光栅化结果。
		VIEWPORT_DEBUG_DRAW_MOTION_VECTORS,				// 绘制运动向量缓冲，用于时域抗锯齿（TAA）运动补偿。
		VIEWPORT_DEBUG_DRAW_INTERNAL_BUFFER,			// 绘制引擎内部后处理使用的缓冲区，以便调试中间输出。
	};

	// 设置调试绘制模式
	// 这些模式涵盖了从简单的“无光照”显示，到法线缓冲、阴影图集、SSAO、全局光照缓存、运动向量等多种底层渲染数据的可视化，帮助开发者精准定位材质、光照、后处理、剔除和集群等渲染管线环节的问题
	virtual void viewport_set_debug_draw(RID p_viewport, ViewportDebugDraw p_draw) = 0;

	// 启用测量渲染时间
	virtual void viewport_set_measure_render_time(RID p_viewport, bool p_enable) = 0;
	// 获取cpu渲染时间
	virtual double viewport_get_measured_render_time_cpu(RID p_viewport) const = 0;
	// 获取GPU渲染时间
	virtual double viewport_get_measured_render_time_gpu(RID p_viewport) const = 0;

	// 从屏幕附属获取视口
	virtual RID viewport_find_from_screen_attachment(DisplayServer::WindowID p_id = DisplayServer::MAIN_WINDOW_ID) const = 0;

	// 视口的VRS（可变速率着色）模式
	enum ViewportVRSMode {
		VIEWPORT_VRS_DISABLED,		// 禁用
		VIEWPORT_VRS_TEXTURE,		// 使用用户提供的密度纹理来控制不同区域的着色率；对立体渲染（VR）场景，需要使用纹理图集为每个视图指定独立的着色率纹理
		VIEWPORT_VRS_XR,			// 在 XR（VR/AR）环境中启用 VRS，着色率纹理由主 XRInterface（如 OpenXR）自动生成并提供，可配合硬件瞳孔追踪或焦点渲染实现注视区高质量渲染
		VIEWPORT_VRS_MAX,
	};

	enum ViewportVRSUpdateMode {
		VIEWPORT_VRS_UPDATE_DISABLED,		// 禁用
		VIEWPORT_VRS_UPDATE_ONCE,			// 一次性
		VIEWPORT_VRS_UPDATE_ALWAYS,			// 每帧都会更新
		VIEWPORT_VRS_UPDATE_MAX,
	};

	// 设置视口的vrs模式
	virtual void viewport_set_vrs_mode(RID p_viewport, ViewportVRSMode p_mode) = 0;
	// 设置视口的vrs更新模式
	virtual void viewport_set_vrs_update_mode(RID p_viewport, ViewportVRSUpdateMode p_mode) = 0;
	// 设置视口的vrs纹理
	virtual void viewport_set_vrs_texture(RID p_viewport, RID p_texture) = 0;

	/* SKY API */

	// 控制 3D 环境背景（Sky）的 Radiance Map 生成方式，以在性能与质量之间做出平衡。
	enum SkyMode {
		SKY_MODE_AUTOMATIC,		// 根据 Shader 使用的变量自动选择处理模式：如果检测到 TIME 或 POSITION，自动切换到实时模式；如果检测到 LIGHT_* 变量或自定义 Uniform，切换到增量模式；否则使用高质量模式生成 Radiance Map。
		SKY_MODE_QUALITY,		// 使用高质量重要性采样算法处理 Radiance Map，可获得更清晰、更准确的反射效果，但会带来较高的 GPU 开销与生成时间。
		SKY_MODE_INCREMENTAL,	// 同样采用高质量重要性采样，但将计算分散到多帧内执行，帧数由 ProjectSettings.rendering/reflections/sky_reflections/roughness_layers 决定，有效降低单帧性能压力。 
		SKY_MODE_REALTIME		// 使用快速过滤算法实时更新 Radiance Map，适用于动态变化的天空场景，牺牲部分质量以换取每帧更新的性能效率；仅支持使用 256×256 的 Radiance 贴图，否则会忽略设置并产生日志警告。
	};

	// 创建一个空的 Sky 资源并在 RenderingServer 中注册
	virtual RID sky_create() = 0;
	// 设置指定 Sky 资源的辐射贴图（Radiance Map）分辨率（每个面为 p_radiance_size × p_radiance_size 像素），影响反射细节
	virtual void sky_set_radiance_size(RID p_sky, int p_radiance_size) = 0;
	// 设置 Sky 的处理模式（SkyMode 枚举），决定 Radiance Map 的生成策略，如一次性高质量、增量分帧或实时快速更新
	virtual void sky_set_mode(RID p_sky, SkyMode p_mode) = 0;
	// 指定用于渲染天空背景、环境光照与反射贴图的材质 Material
	virtual void sky_set_material(RID p_sky, RID p_material) = 0;
	// 离线生成并返回一个包含 Radiance Map（若 p_bake_irradiance=false）或 Irradiance Map（若 p_bake_irradiance=true）的 Image 对象，用于反射或环境光照。
	virtual Ref<Image> sky_bake_panorama(RID p_sky, float p_energy, bool p_bake_irradiance, const Size2i &p_size) = 0;

	/* COMPOSITOR EFFECTS API */

	// 指定后处理（CompositorEffect）在渲染过程中需要访问或生成哪些缓冲数据，以避免不必要的资源开销并优化性能与质量
	enum CompositorEffectFlags {
		COMPOSITOR_EFFECT_FLAG_ACCESS_RESOLVED_COLOR = 1,		// 启用 MSAA 时，若后处理效果需要访问已解析（resolved）的颜色缓冲，必须设置此标志，以保证颜色数据可用且正确传递给效果处理阶段。
		COMPOSITOR_EFFECT_FLAG_ACCESS_RESOLVED_DEPTH = 2,		// 启用 MSAA 时，若后处理效果需要访问已解析的深度缓冲，需设置此标志，保证深度信息被正确提取并用于深度测试或其他效果。
		COMPOSITOR_EFFECT_FLAG_NEEDS_MOTION_VECTORS = 4,		// 后处理效果若依赖运动矢量（Motion Vectors），例如实现运动模糊或基于运动的时间空间效果，需启用该标志以生成并提供运动矢量缓冲
		COMPOSITOR_EFFECT_FLAG_NEEDS_ROUGHNESS = 8,				// 在 Forward+ 渲染模式下，某些后处理效果（如基于材质粗糙度的全局光照调整）需要法线与粗糙度 G-缓冲，开启此标志可确保相应 G-缓冲被输出
		COMPOSITOR_EFFECT_FLAG_NEEDS_SEPARATE_SPECULAR = 16,	// 如果效果需要独立的镜面反射分量（Separate Specular），例如高级反射合成或镜面高光单独处理，需启用此标志以生成单独的镜面反射缓冲。
	};

	// 自定义渲染效果回调在渲染管线中的调用阶段
	enum CompositorEffectCallbackType {
		COMPOSITOR_EFFECT_CALLBACK_TYPE_PRE_OPAQUE,			// 在不透明渲染阶段之前调用，但在深度预渲染（depth prepass）之后
		COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_OPAQUE,		// 在不透明渲染阶段之后调用，但在天空（sky）渲染之前执行
		COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_SKY,			// 在天空渲染完成后调用，但在创建后端缓冲区（back buffers）及（如果启用）次表面散射或屏幕空间反射之前执行
		COMPOSITOR_EFFECT_CALLBACK_TYPE_PRE_TRANSPARENT,	// 在透明渲染阶段之前调用，但在天空渲染并创建后端缓冲区之后执行
		COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_TRANSPARENT,	// 在透明渲染阶段之后调用，但在任何内建后处理效果（post-processing）及输出到渲染目标之前执行
		COMPOSITOR_EFFECT_CALLBACK_TYPE_MAX,
		COMPOSITOR_EFFECT_CALLBACK_TYPE_ANY = -1,
	};

	// 创建组合效果
	virtual RID compositor_effect_create() = 0;
	// 启用组合效果
	virtual void compositor_effect_set_enabled(RID p_effect, bool p_enabled) = 0;
	// 设置回调阶段和回调函数
	virtual void compositor_effect_set_callback(RID p_effect, CompositorEffectCallbackType p_callback_type, const Callable &p_callback) = 0;
	// 设置效果标记
	virtual void compositor_effect_set_flag(RID p_effect, CompositorEffectFlags p_flag, bool p_set) = 0;

	/* COMPOSITOR API */

	// 创建组合器
	virtual RID compositor_create() = 0;

	// 设置组合效果
	virtual void compositor_set_compositor_effects(RID p_compositor, const TypedArray<RID> &p_effects) = 0;

	/* ENVIRONMENT API */

	// 创建环境
	virtual RID environment_create() = 0;

	// 环境背景枚举
	enum EnvironmentBG {
		ENV_BG_CLEAR_COLOR,		// 使用清除颜色做背景
		ENV_BG_COLOR,			// 使用纯色做背景
		ENV_BG_SKY,				// 使用天空做背景
		ENV_BG_CANVAS,			// 使用画布做背景
		ENV_BG_KEEP,			// 不清除背景，使用上一帧渲染的内容作为背景。
		ENV_BG_CAMERA_FEED,		// 在背景中显示相机源
		ENV_BG_MAX
	};

	// 控制环境光（ambient light）的来源方式
	// 也就是说，环境光照和背景是分离的，不是说背景是啥样就用啥样的环境光。
	enum EnvironmentAmbientSource {
		ENV_AMBIENT_SOURCE_BG,			// 从背景中获取环境光
		ENV_AMBIENT_SOURCE_DISABLED,	// 禁用环境光
		ENV_AMBIENT_SOURCE_COLOR,		// 使用指定的颜色作为环境光
		ENV_AMBIENT_SOURCE_SKY,			// 从天空资源中获取环境光。即使背景不是天空，也会使用天空资源中的光照信息。
	};

	// 控制环境反射的来源方式，即在 3D 场景中物体表面反射的背景信息
	enum EnvironmentReflectionSource {
		ENV_REFLECTION_SOURCE_BG,			// 当前设置的背景作为反射源
		ENV_REFLECTION_SOURCE_DISABLED,		// 禁用反射来源
		ENV_REFLECTION_SOURCE_SKY,			// 使用天空资源做反射来源。
	};

	// 设置环境的背景
	virtual void environment_set_background(RID p_env, EnvironmentBG p_bg) = 0;
	// 设置环境背景的天空资源
	virtual void environment_set_sky(RID p_env, RID p_sky) = 0;
	// 设置天空的自定义fov值
	virtual void environment_set_sky_custom_fov(RID p_env, float p_scale) = 0;
	// 设置天空的朝向
	virtual void environment_set_sky_orientation(RID p_env, const Basis &p_orientation) = 0;
	// 设置背景颜色
	virtual void environment_set_bg_color(RID p_env, const Color &p_color) = 0;
	// 设置背景的能量和曝光值
	virtual void environment_set_bg_energy(RID p_env, float p_multiplier, float p_exposure_value) = 0;
	// 设置背景画布的最大层数
	virtual void environment_set_canvas_max_layer(RID p_env, int p_max_layer) = 0;
	// 设置环境光照
	virtual void environment_set_ambient_light(RID p_env, const Color &p_color, EnvironmentAmbientSource p_ambient = ENV_AMBIENT_SOURCE_BG, float p_energy = 1.0, float p_sky_contribution = 0.0, EnvironmentReflectionSource p_reflection_source = ENV_REFLECTION_SOURCE_BG) = 0;
	// 设置环境的相机源 ID。p_camera_feed_id 是相机源的 ID，用于在背景中显示相机源。
	virtual void environment_set_camera_feed_id(RID p_env, int p_camera_feed_id) = 0;

	// 环境发光效果的混合模式,用于控制发光效果与背景图像的融合方式
	enum EnvironmentGlowBlendMode {
		ENV_GLOW_BLEND_MODE_ADDITIVE,		// 加法混合模式，直接将发光效果添加到图像上，常用于粒子、镜头光晕等效果。
		ENV_GLOW_BLEND_MODE_SCREEN,			// 屏幕混合模式，增加亮度，常与 Bloom 效果一起使用。
		ENV_GLOW_BLEND_MODE_SOFTLIGHT,		// 柔光混合模式，修改对比度，突出阴影和高光，适用于昏暗场景。
		ENV_GLOW_BLEND_MODE_REPLACE,		// 替代混合模式，用发光效果替代原图像，常用于全屏模糊或调试效果。
		ENV_GLOW_BLEND_MODE_MIX,			// 混合模式，平衡发光与原图像的亮度，避免过度增加亮度。
	};

	// 启用或禁用环境的发光效果
	/**
		p_enable：启用或禁用发光效果。

		p_levels：发光层级的强度数组，用于控制不同层级的发光效果。

		p_intensity：整体发光强度。

		p_strength：高斯模糊强度，影响发光的扩散程度。

		p_mix：混合强度，控制发光与原始图像的混合程度。

		p_bloom_threshold：HDR 阈值，超过此亮度的区域将产生发光效果。

		p_blend_mode：发光混合模式，决定发光与背景的融合方式。

		p_hdr_bleed_threshold：HDR 漏光阈值，控制亮度溢出区域的起始点。

		p_hdr_bleed_scale：HDR 漏光缩放，调整溢出亮度的强度。

		p_hdr_luminance_cap：HDR 亮度上限，限制最大亮度值。

		p_glow_map_strength：发光贴图强度，影响发光贴图的贡献度。

		p_glow_map：发光贴图的资源 ID，用于提供额外的发光信息。
	 */
	virtual void environment_set_glow(RID p_env, bool p_enable, Vector<float> p_levels, float p_intensity, float p_strength, float p_mix, float p_bloom_threshold, EnvironmentGlowBlendMode p_blend_mode, float p_hdr_bleed_threshold, float p_hdr_bleed_scale, float p_hdr_luminance_cap, float p_glow_map_strength, RID p_glow_map) = 0;

	// 用于启用或禁用发光效果的双三次上采样
	virtual void environment_glow_set_use_bicubic_upscale(bool p_enable) = 0;

	enum EnvironmentToneMapper {
		ENV_TONE_MAPPER_LINEAR,		// 线性映射，简单但可能导致高亮区域丢失细节。
		ENV_TONE_MAPPER_REINHARD,	// Reinhardt 算法，避免高亮区域过曝，适用于一般场景。
		ENV_TONE_MAPPER_FILMIC,		// 电影级算法，提供更自然的高光过渡，适合电影风格渲染。
		ENV_TONE_MAPPER_ACES		// ACES 算法，提供高对比度和色彩还原，适用于高质量渲染。
	};

	// 设置色调映射的方式
	virtual void environment_set_tonemap(RID p_env, EnvironmentToneMapper p_tone_mapper, float p_exposure, float p_white) = 0;
	// 后期调整用于在渲染完成后修改图像的色彩和亮度，以实现特定的视觉效果。
	// p_enable：启用或禁用后期调整。
	// p_brightness：亮度调整。
	// p_contrast：对比度调整。
	// p_saturation：饱和度调整。
	// p_use_1d_color_correction：是否使用 1D 色彩校正。
	// p_color_correction：色彩校正纹理资源。
	virtual void environment_set_adjustment(RID p_env, bool p_enable, float p_brightness, float p_contrast, float p_saturation, bool p_use_1d_color_correction, RID p_color_correction) = 0;

	// 启用或禁用环境的屏幕空间反射（SSR）效果，并设置相关参数
	// p_enable：启用或禁用 SSR。
	// p_max_steps：最大反射步数，控制反射的计算精度。
	// p_fade_in：反射渐显时间，单位为秒。
	// p_fade_out：反射渐隐时间，单位为秒。
	// p_depth_tolerance：深度容忍度，用于控制反射的深度精度。
	virtual void environment_set_ssr(RID p_env, bool p_enable, int p_max_steps, float p_fade_in, float p_fade_out, float p_depth_tolerance) = 0;

	enum EnvironmentSSRRoughnessQuality {
		ENV_SSR_ROUGHNESS_QUALITY_DISABLED,		// 禁用粗糙度过滤。
		ENV_SSR_ROUGHNESS_QUALITY_LOW,			// 低质量的粗糙度过滤。
		ENV_SSR_ROUGHNESS_QUALITY_MEDIUM,		// 中等质量的粗糙度过滤。
		ENV_SSR_ROUGHNESS_QUALITY_HIGH,			// 高质量的粗糙度过滤。
	};

	// 设置 SSR 的粗糙度质量
	virtual void environment_set_ssr_roughness_quality(EnvironmentSSRRoughnessQuality p_quality) = 0;

	// 启用或禁用环境的 SSAO 效果
	// p_enable：启用或禁用 SSAO
	// p_radius：影响范围，控制遮蔽效果的扩展距离。
	// p_intensity：强度，控制遮蔽效果的亮度。
	// p_power：分布，控制遮蔽效果的锐度。
	// p_detail：细节，控制额外细节层级的强度。
	// p_horizon：阈值，表示是否认为表面上的某一点被遮蔽，作为从地平线映射到 0.0-1.0 范围的角度。值越高，遮蔽效果越弱。
	// p_sharpness：锐度，控制 SSAO 效果在物体边缘的模糊程度。
	// p_light_affect：直接光影响，控制 SSAO 效果在直接光照下的强度。
	// p_ao_channel_affect：AO 通道影响，控制 AO 纹理定义的材料上 SSAO 效果的强度。值较高时，AO 效果在 AO 纹理定义的区域更明显。
	virtual void environment_set_ssao(RID p_env, bool p_enable, float p_radius, float p_intensity, float p_power, float p_detail, float p_horizon, float p_sharpness, float p_light_affect, float p_ao_channel_affect) = 0;

	// SSAO的质量
	enum EnvironmentSSAOQuality {
		ENV_SSAO_QUALITY_VERY_LOW,
		ENV_SSAO_QUALITY_LOW,
		ENV_SSAO_QUALITY_MEDIUM,
		ENV_SSAO_QUALITY_HIGH,
		ENV_SSAO_QUALITY_ULTRA,
	};

	// 设置 SSAO 的质量级别
	// p_quality：质量级别，类型为 EnvironmentSSAOQuality 枚举。
	// p_half_size：是否使用半尺寸渲染，减少计算量。
	// p_adaptive_target：自适应目标，控制 SSAO 效果的亮度适应。
	// p_blur_passes：模糊次数，控制 SSAO 效果的模糊程度。
	// p_fadeout_from：渐隐起始距离，控制 SSAO 效果开始渐隐的距离。
	// p_fadeout_to：渐隐结束距离，控制 SSAO 效果完全渐隐的距离。
	virtual void environment_set_ssao_quality(EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) = 0;

	// 屏幕空间间接光照（SSIL）效果
	// p_enable：启用或禁用 SSIL。
	// p_radius：影响范围，控制间接光照效果的扩展距离。
	// p_intensity：强度，控制间接光照效果的亮度。
	// p_sharpness：锐度，控制间接光照效果在物体边缘的模糊程度。
	// p_normal_rejection：法线排斥度，控制计算间接光照时对法线方向的排斥程度。
	virtual void environment_set_ssil(RID p_env, bool p_enable, float p_radius, float p_intensity, float p_sharpness, float p_normal_rejection) = 0;

	// SSIL的质量
	enum EnvironmentSSILQuality {
		ENV_SSIL_QUALITY_VERY_LOW,
		ENV_SSIL_QUALITY_LOW,
		ENV_SSIL_QUALITY_MEDIUM,
		ENV_SSIL_QUALITY_HIGH,
		ENV_SSIL_QUALITY_ULTRA,
	};

	// 设置 SSIL 的质量级别
	// p_quality：质量级别，类型为 EnvironmentSSILQuality 枚举。
	// p_half_size：是否使用半尺寸渲染，减少计算量。
	// p_adaptive_target：自适应目标，控制 SSIL 效果的亮度适应。
	// p_blur_passes：模糊次数，控制 SSIL 效果的模糊程度。
	// p_fadeout_from：渐隐起始距离，控制 SSIL 效果开始渐隐的距离。
	// p_fadeout_to：渐隐结束距离，控制 SSIL 效果完全渐隐的距离。
	virtual void environment_set_ssil_quality(EnvironmentSSILQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) = 0;

	enum EnvironmentSDFGIYScale {
		ENV_SDFGI_Y_SCALE_50_PERCENT,
		ENV_SDFGI_Y_SCALE_75_PERCENT,
		ENV_SDFGI_Y_SCALE_100_PERCENT,
	};

	// 启用或禁用环境的带符号距离场全局光照（SDFGI，Signed Distance Field Global Illumination）效果，并设置相关参数。
	// p_enable：启用或禁用 SDFGI。
	// p_cascades：级联数量，控制全局光照的细节层次。较高的值提供更高的细节，但性能开销更大。
	// p_min_cell_size：最小单元格大小，影响最细级别的细节。较小的值提供更精细的光照效果，但可能降低性能。
	// p_y_scale：垂直尺度，控制 SDFGI 探针在垂直方向上的分布。默认值为 100%，表示水平和垂直方向上的分布相同。
	// p_use_occlusion：启用或禁用遮挡计算。启用时，SDFGI 将考虑探针之间的遮挡，以减少光泄漏。
	// p_bounce_feedback：反弹反馈，控制间接光照的反弹次数。较高的值提供更真实的间接光照效果，但性能开销更大。
	// p_read_sky：启用或禁用从天空读取光照。启用时，天空光源将影响间接光照。
	// p_energy：光照强度，控制间接光照的亮度。
	// p_normal_bias：法线偏移，控制探针射线反弹时的法线偏移量。用于减少条纹伪影。
	// p_probe_bias：探针偏移，控制探针射线反弹时的偏移量。用于减少条纹伪影。
	virtual void environment_set_sdfgi(RID p_env, bool p_enable, int p_cascades, float p_min_cell_size, EnvironmentSDFGIYScale p_y_scale, bool p_use_occlusion, float p_bounce_feedback, bool p_read_sky, float p_energy, float p_normal_bias, float p_probe_bias) = 0;

	enum EnvironmentSDFGIRayCount {
		ENV_SDFGI_RAY_COUNT_4,
		ENV_SDFGI_RAY_COUNT_8,
		ENV_SDFGI_RAY_COUNT_16,
		ENV_SDFGI_RAY_COUNT_32,
		ENV_SDFGI_RAY_COUNT_64,
		ENV_SDFGI_RAY_COUNT_96,
		ENV_SDFGI_RAY_COUNT_128,
		ENV_SDFGI_RAY_COUNT_MAX,
	};

	// 设置sdfgi的光线数量
	virtual void environment_set_sdfgi_ray_count(EnvironmentSDFGIRayCount p_ray_count) = 0;

	enum EnvironmentSDFGIFramesToConverge {
		ENV_SDFGI_CONVERGE_IN_5_FRAMES,
		ENV_SDFGI_CONVERGE_IN_10_FRAMES,
		ENV_SDFGI_CONVERGE_IN_15_FRAMES,
		ENV_SDFGI_CONVERGE_IN_20_FRAMES,
		ENV_SDFGI_CONVERGE_IN_25_FRAMES,
		ENV_SDFGI_CONVERGE_IN_30_FRAMES,
		ENV_SDFGI_CONVERGE_MAX
	};

	// 设置 SDFGI 效果的收敛速度，即间接光照从初始状态到最终稳定状态所需的帧数
	// 较高的帧数（例如 30 帧）将导致更平滑的光照过渡，但可能在场景加载或动态光源快速移动时出现延迟。
	// 较低的帧数（例如 5 帧）将加快收敛速度，但可能导致间接光照出现明显的斑点或闪烁。
	virtual void environment_set_sdfgi_frames_to_converge(EnvironmentSDFGIFramesToConverge p_frames) = 0;

	enum EnvironmentSDFGIFramesToUpdateLight {
		ENV_SDFGI_UPDATE_LIGHT_IN_1_FRAME,
		ENV_SDFGI_UPDATE_LIGHT_IN_2_FRAMES,
		ENV_SDFGI_UPDATE_LIGHT_IN_4_FRAMES,
		ENV_SDFGI_UPDATE_LIGHT_IN_8_FRAMES,
		ENV_SDFGI_UPDATE_LIGHT_IN_16_FRAMES,
		ENV_SDFGI_UPDATE_LIGHT_MAX,
	};

	// 用于设置动态光源的间接光照更新频率，即每隔多少帧更新一次动态光源的间接光照
	// 较高的更新频率（例如每 1 帧更新）将使动态光源的间接光照更即时地反映其变化，但可能增加性能开销。
	// 较低的更新频率（例如每 16 帧更新）将减少性能负担，但可能导致间接光照对动态光源变化的响应延迟。
	virtual void environment_set_sdfgi_frames_to_update_light(EnvironmentSDFGIFramesToUpdateLight p_update) = 0;

	enum EnvironmentFogMode {
		ENV_FOG_MODE_EXPONENTIAL,
		ENV_FOG_MODE_DEPTH,
	};

	// 启用或禁用传统雾效，并设置相关参数
	// p_enable：启用或禁用雾效。
	// p_light_color：雾效的光照颜色。
	// p_light_energy：光照强度
	// p_sun_scatter：太阳散射强度。
	// p_density：雾效的密度。
	// p_height：雾效的高度。
	// p_height_density：雾效的高度密度。
	// p_aerial_perspective：大气透视效果。
	// p_sky_affect：天空影响度。
	// p_mode：雾效模式，默认为指数模式。
	virtual void environment_set_fog(RID p_env, bool p_enable, const Color &p_light_color, float p_light_energy, float p_sun_scatter, float p_density, float p_height, float p_height_density, float p_aerial_perspective, float p_sky_affect, EnvironmentFogMode p_mode = EnvironmentFogMode::ENV_FOG_MODE_EXPONENTIAL) = 0;
	// 用于设置深度雾效的起始和结束距离，以及曲线
	// p_curve：雾效的曲线。
	// p_begin：雾效的起始距离。
	// p_end：雾效的结束距离
	virtual void environment_set_fog_depth(RID p_env, float p_curve, float p_begin, float p_end) = 0;

	// 启用或禁用体积雾效，并设置相关参数
	// 体积雾效使用三维缓冲区计算和存储雾效密度值，允许雾效与光源和阴影交互，提供更真实的效果。适用于需要局部雾效的场景。
	// p_enable：启用或禁用体积雾效。
	// p_density：雾效的密度。
	// p_albedo：雾效的反照率颜色。
	// p_emission：雾效的发射颜色。
	// p_emission_energy：发射能量。
	// p_anisotropy：各向异性，控制光散射方向。
	// p_length：雾效的长度。
	// p_detail_spread：细节扩展。
	// p_gi_inject：全局光照注入强度。
	// p_temporal_reprojection：启用或禁用时间重投影。
	// p_temporal_reprojection_amount：时间重投影量。
	// p_ambient_inject：环境光注入强度。
	// p_sky_affect：天空影响度。
	virtual void environment_set_volumetric_fog(RID p_env, bool p_enable, float p_density, const Color &p_albedo, const Color &p_emission, float p_emission_energy, float p_anisotropy, float p_length, float p_detail_spread, float p_gi_inject, bool p_temporal_reprojection, float p_temporal_reprojection_amount, float p_ambient_inject, float p_sky_affect) = 0;
	// 设置体积雾效的体积大小
	// 调整体积大小和深度可以影响体积雾效的细节和性能。
	virtual void environment_set_volumetric_fog_volume_size(int p_size, int p_depth) = 0;
	// 启用或禁用体积雾效的过滤
	// 启用过滤可以平滑体积雾效的边缘，减少锯齿状伪影。
	virtual void environment_set_volumetric_fog_filter_active(bool p_enable) = 0;

	// 烘焙环境的全景图像，可用于环境光照贴图（IBL）或反射探针
	// p_bake_irradiance：是否烘焙辐照度。
	// p_size：生成图像的尺寸
	virtual Ref<Image> environment_bake_panorama(RID p_env, bool p_bake_irradiance, const Size2i &p_size) = 0;

	// 启用或禁用屏幕空间粗糙度限制器，以优化材质的反射效果
	// 启用此功能可以减少高粗糙度材质的反射伪影，提升性能。
	// p_enable：启用或禁用限制器。
	// p_amount：限制器的强度。
	// p_limit：最大粗糙度限制。
	virtual void screen_space_roughness_limiter_set_active(bool p_enable, float p_amount, float p_limit) = 0;

	enum SubSurfaceScatteringQuality {
		SUB_SURFACE_SCATTERING_QUALITY_DISABLED,
		SUB_SURFACE_SCATTERING_QUALITY_LOW,
		SUB_SURFACE_SCATTERING_QUALITY_MEDIUM,
		SUB_SURFACE_SCATTERING_QUALITY_HIGH,
	};

	// 设置次表面散射（SSS）的质量，以模拟皮肤等材质的光散射效果
	virtual void sub_surface_scattering_set_quality(SubSurfaceScatteringQuality p_quality) = 0;
	// 设置次表面散射的比例，以调整光在材质中的散射深度
	virtual void sub_surface_scattering_set_scale(float p_scale, float p_depth_scale) = 0;

	/* CAMERA EFFECTS */

	// 创建相机属性
	virtual RID camera_attributes_create() = 0;

	// 景深模糊的质量
	enum DOFBlurQuality {
		DOF_BLUR_QUALITY_VERY_LOW,
		DOF_BLUR_QUALITY_LOW,
		DOF_BLUR_QUALITY_MEDIUM,
		DOF_BLUR_QUALITY_HIGH,
	};

	// 设置景深模糊的质量，以及是否使用抖动
	virtual void camera_attributes_set_dof_blur_quality(DOFBlurQuality p_quality, bool p_use_jitter) = 0;

	// “散景”（Bokeh）高光的几何形状
	enum DOFBokehShape {
		DOF_BOKEH_BOX,		// 方形
		DOF_BOKEH_HEXAGON,	// 六边形
		DOF_BOKEH_CIRCLE		// 圆形
	};

	// 设置散景的几何形状
	virtual void camera_attributes_set_dof_blur_bokeh_shape(DOFBokehShape p_shape) = 0;

	// 设置景深模糊效果的各项参数。这些参数在功能上与 CameraAttributesPractical 中的对应字段完全一致，用以控制焦外成像的开启、焦距位置、过渡带宽和整体模糊强度
	// p_far_enable（bool）：是否启用“远处”模糊（焦点之后）。
	// p_far_distance（float）：远处焦平面距离，即开始出现模糊的深度位置。
	// p_far_transition（float）：从清晰到模糊的过渡带宽。
	// p_near_enable（bool）：是否启用“近处”模糊（焦点之前）。
	// p_near_distance（float）：近处焦平面距离，即从镜头前景开始模糊的深度位置。
	// p_near_transition（float）：近处清晰–模糊过渡带宽。
	// p_amount（float）：整体模糊强度系数，叠加远近模糊效果。
	virtual void camera_attributes_set_dof_blur(RID p_camera_attributes, bool p_far_enable, float p_far_distance, float p_far_transition, bool p_near_enable, float p_near_distance, float p_near_transition, float p_amount) = 0;
	// 设置渲染器所使用的固定曝光值（Exposure Value, EV）及其归一化系数，用于在渲染计算中调整场景整体亮度并压缩动态范围
	// p_multiplier（float）：曝光乘数，直接影响最终像素亮度。
	// p_exposure_normalization（float）：归一化系数，通常根据 EV100 计算，用以校正物理曝光模型
	virtual void camera_attributes_set_exposure(RID p_camera_attributes, float p_multiplier, float p_exposure_normalization) = 0;
	// 启用或禁用动态曝光调节，使相机可根据场景亮度自动增减曝光值，模仿真实相机的自动 ISO 调节功能
	// p_enable（bool）：是否开启自动曝光。
	// p_min_sensitivity / p_max_sensitivity（float）：ISO 感光度范围，用于限定自动曝光的上下限。
	// p_speed（float）：曝光调整速度，值越大响应越快。
	// p_scale（float）：缩放因子，用于对感光度曲线进行线性调整。
	virtual void camera_attributes_set_auto_exposure(RID p_camera_attributes, bool p_enable, float p_min_sensitivity, float p_max_sensitivity, float p_speed, float p_scale) = 0;

	/* SCENARIO API */
	// 场景（Scenario） 是 3D 世界的可视上下文，用于统一管理可视实例、环境（Environment）、相机后期参数（CameraAttributes）以及后期合成管线（Compositor）。

	virtual RID scenario_create() = 0;

	// 设置环境
	virtual void scenario_set_environment(RID p_scenario, RID p_environment) = 0;
	// 设置回落环境
	virtual void scenario_set_fallback_environment(RID p_scenario, RID p_environment) = 0;
	// 设置相机属性
	virtual void scenario_set_camera_attributes(RID p_scenario, RID p_camera_attributes) = 0;
	// 设置组合器
	virtual void scenario_set_compositor(RID p_scenario, RID p_compositor) = 0;

	/* INSTANCING API */

	// 所有的实例类型
	enum InstanceType {
		INSTANCE_NONE,		// 未指定类型的实例，占位或默认值
		INSTANCE_MESH,		// 一个普通的网格实例，用于渲染 MeshInstance3D 等节点。
		INSTANCE_MULTIMESH,		// 批量渲染的多网格实例，对应 MultiMeshInstance3D，可显著减少 draw call。
		INSTANCE_PARTICLES,		// GPU 粒子发射器实例，对应 GPUParticles3D，用于高效渲染粒子系统。
		INSTANCE_PARTICLES_COLLISION,		// GPU 粒子碰撞形状实例，用于粒子与世界交互的碰撞检测。
		INSTANCE_LIGHT,		// 灯光实例，如 DirectionalLight3D、OmniLight3D 等。
		INSTANCE_REFLECTION_PROBE,	// 反射探针实例，用于环境盒映射和光照反射。
		INSTANCE_DECAL,		// 贴花实例，例如在地面或墙壁上渲染污渍、标记等贴花效果。
		INSTANCE_VOXEL_GI,		// 体素全局光照实例，驱动体素化 GI 系统。 
		INSTANCE_LIGHTMAP,		// 光照贴图实例，用于静态光照贴图渲染。
		INSTANCE_OCCLUDER,		// 遮挡剔除实例，对应可用于屏蔽其他对象的遮挡体。
		INSTANCE_VISIBLITY_NOTIFIER,		// 可见性通知器实例，当对象进入或离开视野时触发事件。
		INSTANCE_FOG_VOLUME,		// 雾体积实例，用于渲染区域雾效。
		INSTANCE_MAX,

		INSTANCE_GEOMETRY_MASK = (1 << INSTANCE_MESH) | (1 << INSTANCE_MULTIMESH) | (1 << INSTANCE_PARTICLES)
	};

	// 创建一个新的渲染实例，并同时设置它的基础资源（Base）和场景（Scenario）
	virtual RID instance_create2(RID p_base, RID p_scenario);

	// 创建空实例
	virtual RID instance_create() = 0;

	// 将实例的基础资源设置为任意可显示的 3D 对象（网格、粒子、灯光、反射探针、贴花、光照贴图、体素 GI、可见性通知器等），否则该实例不会在场景中被渲染。
	virtual void instance_set_base(RID p_instance, RID p_base) = 0;
	// 将实例关联到指定的场景（Scenario），决定它在哪个 3D 世界中被渲染。
	virtual void instance_set_scenario(RID p_instance, RID p_scenario) = 0;
	// 定义实例参与渲染的图层掩码，对应于 VisualInstance3D.layers，可控制实例在哪些渲染层中可见。
	virtual void instance_set_layer_mask(RID p_instance, uint32_t p_mask) = 0;
	// 设置深度排序偏移量，并在使用实例原点还是包围盒中心进行深度排序之间切换，影响渲染顺序和遮挡关系。
	virtual void instance_set_pivot_data(RID p_instance, float p_sorting_offset, bool p_use_aabb_center) = 0;
	// 设置实例的世界空间变换，相当于 Node3D.global_transform，控制实例的位置、旋转和缩放。
	virtual void instance_set_transform(RID p_instance, const Transform3D &p_transform) = 0;
	// 开启或关闭物理插值，使实例在物理帧之间平滑移动。
	virtual void instance_set_interpolated(RID p_instance, bool p_interpolated) = 0;
	// 清除当前物理插值状态，保证下一个物理步骤立即生效，适用于瞬时位置跳跃等场景。
	virtual void instance_reset_physics_interpolation(RID p_instance) = 0;
	// 附加一个唯一的对象 ID，用于编辑器或自定义剔除查询（instances_cull_ *系列），确保基于 AABB、凸形或射线的剔除能正确识别对应实例。
	virtual void instance_attach_object_instance_id(RID p_instance, ObjectID p_id) = 0;
	// 为网格实例的特定混合形状（BlendShape）设置权重，驱动形态变化或面部动画等效果。
	virtual void instance_set_blend_shape_weight(RID p_instance, int p_shape, float p_weight) = 0;
	// 为实例的指定表面索引应用覆盖材质（Override Material），等价于 MeshInstance3D.set_surface_override_material，可在运行时替换单一表面材质。
	virtual void instance_set_surface_override_material(RID p_instance, int p_surface, RID p_material) = 0;
	// 启用或禁用实例的渲染显示，相当于 Node3D.visible，可临时隐藏实例而不删除。
	virtual void instance_set_visible(RID p_instance, bool p_visible) = 0;

	// 为实例设置自定义的包围盒（AABB），在视锥剔除时使用，等价于 GeometryInstance3D.custom_aabb，可优化剔除表现或扩展可见范围。
	virtual void instance_set_custom_aabb(RID p_instance, AABB aabb) = 0;

	// 将一个骨骼资源（如 Skeleton3D）附加到几何实例上，用于驱动网格的骨骼蒙皮动画；如果之前已附加过其他骨骼，则会先移除旧的骨骼再绑定新骨骼。
	virtual void instance_attach_skeleton(RID p_instance, RID p_skeleton) = 0;

	// 在视锥剔除时，为实例的 AABB 增加一个额外的边界余量，以避免对象在摄像机边缘处被过早剔除；等价于 GeometryInstance3D.extra_cull_margin。
	virtual void instance_set_extra_visibility_margin(RID p_instance, real_t p_margin) = 0;
	// 指定另一个实例作为可见性父级，当父级在场景中被隐藏时，子实例也会随之隐藏；等价于 Node3D.visibility_parent。
	virtual void instance_set_visibility_parent(RID p_instance, RID p_parent_instance) = 0;

	// 启用后会同时忽略视锥剔除和遮挡剔除，保证该实例始终被渲染；与仅忽略遮挡剔除的 GeometryInstance3D.ignore_occlusion_culling 不同。
	virtual void instance_set_ignore_culling(RID p_instance, bool p_enabled) = 0;

	// Don't use these in a game!
	// 返回与给定轴对齐包围盒相交的所有实例的对象 ID 列表；仅考虑继承自 VisualInstance3D 的节点，且必须提供待查询的场景（scenario）RID。
	virtual Vector<ObjectID> instances_cull_aabb(const AABB &p_aabb, RID p_scenario = RID()) const = 0;
	// 返回与由一组平面定义的凸形体相交的所有实例的对象 ID 列表；同样需要场景 RID，且会强制更新所有待渲染资源。
	virtual Vector<ObjectID> instances_cull_ray(const Vector3 &p_from, const Vector3 &p_to, RID p_scenario = RID()) const = 0;
	// 返回被指定射线（从 p_from 到 p_to）穿过的所有实例的对象 ID 列表；常用于编辑器中的拾取或可视化调试。
	virtual Vector<ObjectID> instances_cull_convex(const Vector<Plane> &p_convex, RID p_scenario = RID()) const = 0;

	// 根据给定的轴对齐包围盒（AABB）查询，与其相交的所有实例 ID，并返回 PackedInt64Array。
	PackedInt64Array _instances_cull_aabb_bind(const AABB &p_aabb, RID p_scenario = RID()) const;
	// 查询一条从 p_from 到 p_to 的射线与之相交的所有实例 ID。
	PackedInt64Array _instances_cull_ray_bind(const Vector3 &p_from, const Vector3 &p_to, RID p_scenario = RID()) const;
	// 查询与由一组平面定义的凸形体相交的所有实例 ID。
	PackedInt64Array _instances_cull_convex_bind(const TypedArray<Plane> &p_convex, RID p_scenario = RID()) const;

	// 控制几何实例在渲染服务器层面的行为
	enum InstanceFlags {
		INSTANCE_FLAG_USE_BAKED_LIGHT,		// 允许该实例参与烘焙光照计算。
		INSTANCE_FLAG_USE_DYNAMIC_GI,		// 允许该实例参与动态全局光照（Voxel GI、SDFGI 等）。
		INSTANCE_FLAG_DRAW_NEXT_FRAME_IF_VISIBLE,		// 如果当前帧可见，则请求在下一帧强制绘制该实例。
		INSTANCE_FLAG_IGNORE_OCCLUSION_CULLING,	// 忽略遮挡剔除，始终绘制该实例（不影响视锥剔除）。
		INSTANCE_FLAG_MAX
	};

	// 定义了实例投射阴影的不同模式
	enum ShadowCastingSetting {
		SHADOW_CASTING_SETTING_OFF,				// 不投射阴影，可用于微小或无需阴影的对象以提升性能。
		SHADOW_CASTING_SETTING_ON,				// 正常投射阴影，仅考虑视锥剔除的可见面。
		SHADOW_CASTING_SETTING_DOUBLE_SIDED,	// 双面投射阴影，不剔除背面，可能更准确但性能略有下降。
		SHADOW_CASTING_SETTING_SHADOWS_ONLY,	// 仅渲染阴影，不渲染实体网格本身，常用于视觉效果或调试。
	};

	// 渐隐效果的模式
	enum VisibilityRangeFadeMode {
		VISIBILITY_RANGE_FADE_DISABLED,		// 禁用距离渐隐效果，实例要么完全可见，要么完全被剔除。
		VISIBILITY_RANGE_FADE_SELF,			// 仅对自身进行距离渐隐，实例在接近或远离摄像机时根据设定范围平滑淡入淡出。
		VISIBILITY_RANGE_FADE_DEPENDENCIES,		// 对所有依赖于此实例的子实例一起应用距离渐隐，常用于绑定骨骼或粒子系统的整体渐隐。
	};

	// 启用或禁用指定的 InstanceFlags 标志，用于控制诸如烘焙光照、动态 GI、下一帧强制绘制、忽略遮挡剔除等行为。
	virtual void instance_geometry_set_flag(RID p_instance, InstanceFlags p_flags, bool p_enabled) = 0;
	// 设置实例的阴影投射模式，支持关闭、单面投射、双面投射或仅投射阴影四种选项，等同于 GeometryInstance3D.cast_shadow。
	virtual void instance_geometry_set_cast_shadows_setting(RID p_instance, ShadowCastingSetting p_shadow_casting_setting) = 0;
	// 用指定材质覆盖实例的全部表面，等同于 MeshInstance3D.set_surface_override_material。
	virtual void instance_geometry_set_material_override(RID p_instance, RID p_material) = 0;
	// 在原有材质之上叠加第二套材质，可用于环境贴花或高光效果。
	virtual void instance_geometry_set_material_overlay(RID p_instance, RID p_material) = 0;
	// 设置实例的最小和最大可见距离及其渐隐边缘，并指定 VisibilityRangeFadeMode 渐隐模式，用于摄像机距离相关的剔除和淡入淡出控制。
	virtual void instance_geometry_set_visibility_range(RID p_instance, float p_min, float p_max, float p_min_margin, float p_max_margin, VisibilityRangeFadeMode p_fade_mode) = 0;
	// 为实例绑定一个光照贴图资源，并指定 UV 变换矩阵及切片索引，支持立方体贴图和 2D 光照图。
	virtual void instance_geometry_set_lightmap(RID p_instance, RID p_lightmap, const Rect2 &p_lightmap_uv_scale, int p_lightmap_slice) = 0;
	// 设置实例的 LOD（Level of Detail）偏移值，用于驱动多 LOD 网格的切换距离。
	virtual void instance_geometry_set_lod_bias(RID p_instance, float p_lod_bias) = 0;
	//调整实例的整体透明度（0.0–1.0），等同于材质中透明通道的统一倍数缩放。
	virtual void instance_geometry_set_transparency(RID p_instance, float p_transparency) = 0;

	// 在指定的几何体实例上设置一个 per-instance 着色器统一变量，其名称由 StringName 指定，值由 Variant 提供，等效于 GeometryInstance3D.set_instance_shader_parameter()。
	// 要使参数可按实例生效，着色器中必须使用 instance uniform 而非普通 uniform 声明，否则修改将作用于所有使用同一 ShaderMaterial 的实例。
	virtual void instance_geometry_set_shader_parameter(RID p_instance, const StringName &, const Variant &p_value) = 0;
	// 返回指定几何体实例上当前生效的 per-instance 着色器统一变量的值，等效于 GeometryInstance3D.get_instance_shader_parameter()。
	virtual Variant instance_geometry_get_shader_parameter(RID p_instance, const StringName &) const = 0;
	// 返回着色器中为该 per-instance 参数声明时的默认值，用于在未调用 set 时查询默认行为，等效于 GeometryInstance3D.get_instance_shader_parameter() 获取的初始值。
	virtual Variant instance_geometry_get_shader_parameter_default_value(RID p_instance, const StringName &) const = 0;
	// 以 PropertyInfo 格式（包含 name、class_name、type、hint、hint_string、usage 等字段）返回所有在该几何体实例上可用的 per-instance 着色器统一变量列表，等效于 GeometryInstance3D.get_shader_parameter_list()。
	virtual void instance_geometry_get_shader_parameter_list(RID p_instance, List<PropertyInfo> *p_parameters) const = 0;

	/* Bake 3D objects */

	// BakeChannels 枚举定义了 bake_render_uv2 返回的多张烘焙图像中，各张图像所代表的通道索引；
	enum BakeChannels {
		BAKE_CHANNEL_ALBEDO_ALPHA,		// 输出图像格式为 Image.FORMAT_RGBA8，.rgb 通道存储漫反射颜色（Albedo），.a 通道存储透明度（Alpha）。
		BAKE_CHANNEL_NORMAL,			// 输出图像格式为 Image.FORMAT_RGBA8，.rgb 通道存储每像素法线，按 normal * 0.5 + 0.5 编码，.a 通道未使用。
		BAKE_CHANNEL_ORM,				// 输出图像格式为 Image.FORMAT_RGBA8，.r 通道存储环境遮蔽（AO），.g 存储粗糙度（Roughness），.b 存储金属度（Metallic），.a 存储次表面散射量（SSS Amount）。
		BAKE_CHANNEL_EMISSION			// 输出图像格式为 Image.FORMAT_RGBAH，.rgb 通道存储自发光颜色（Emission），.a 通道未使用。
	};

	// 在第二 UV 通道上，对给定的基础资源进行逐通道渲染烘焙，按指定分辨率输出一组 Image。
	virtual TypedArray<Image> bake_render_uv2(RID p_base, const TypedArray<RID> &p_material_overrides, const Size2i &p_image_size) = 0;

	/* CANVAS (2D) */

	// 生成一个新的、空的 2D 画布，返回对应的 RID 以供后续引用和操作
	virtual RID canvas_create() = 0;
	// 在指定偏移下复制（镜像）单个画布项，等同于 canvas_set_item_repeat(item, mirroring, 1)
	virtual void canvas_set_item_mirroring(RID p_canvas, RID p_item, const Point2 &p_mirroring) = 0;
	//  按 repeat_size 偏移重复绘制 times 次，适合网格状平铺效果
	virtual void canvas_set_item_repeat(RID p_item, const Point2 &p_repeat_size, int p_repeat_times) = 0;
	// 对整个画布内的所有绘制命令施加色彩乘法调制，类似于对所有节点统一设置 modulate 属性
	virtual void canvas_set_modulate(RID p_canvas, const Color &p_color) = 0;
	// 可将某个画布挂到另一画布之下，并指定相对缩放
	virtual void canvas_set_parent(RID p_canvas, RID p_parent, float p_scale) = 0;

	// 切换画布是否响应全局 DPI/stretch 缩放，true 时禁用自动缩放，适合像素级精确渲染
	virtual void canvas_set_disable_scale(bool p_disable) = 0;

	/* CANVAS TEXTURE */

	/**
	 *
	 * 2D CanvasTexture 的管理方法，可在不依赖节点树的情况下创建并操作多通道纹理资源。利用 canvas_texture_create() 可以生成一个新的 CanvasTexture 并返回对应的 RID，用于后续的所有 canvas_texture_* 调用；通过 CanvasTextureChannel 枚举可指定漫反射、法线和高光三种通道，并使用 canvas_texture_set_channel() 为各通道绑定不同的纹理资源。canvas_texture_set_shading_parameters() 则允许配置基础颜色与高光强度，以模拟材质的漫反射与镜面反射特性；额外的 canvas_texture_set_texture_filter() 与 canvas_texture_set_texture_repeat() 可分别统一设置过滤与重复模式，影响后续所有绘制命令的采样与平铺行为。
	 */

	// 创建一个新的 CanvasTexture 资源，并返回其 RID
	virtual RID canvas_texture_create() = 0;

	// 三个纹理通道
	enum CanvasTextureChannel {
		CANVAS_TEXTURE_CHANNEL_DIFFUSE,		// 漫反射
		CANVAS_TEXTURE_CHANNEL_NORMAL,		// 法线
		CANVAS_TEXTURE_CHANNEL_SPECULAR,	// 镜面
	};
	// 为指定的 CanvasTexture 的某个通道绑定新的纹理资源 RID
	virtual void canvas_texture_set_channel(RID p_canvas_texture, CanvasTextureChannel p_channel, RID p_texture) = 0;
	// 设置 CanvasTexture 的基础颜色 (base_color) 和高光强度 (shininess)，等价于 CanvasTexture.specular_color 与 CanvasTexture.specular_shininess，用于控制材质的镜面反射效果。
	virtual void canvas_texture_set_shading_parameters(RID p_canvas_texture, const Color &p_base_color, float p_shininess) = 0;

	// Takes effect only for new draw commands.
	// 设置后续绘制命令使用的采样过滤模式（如线性过滤或最近邻采样），影响 CanvasTexture 在渲染时的图像质量与抗锯齿表现。
	virtual void canvas_texture_set_texture_filter(RID p_canvas_texture, CanvasItemTextureFilter p_filter) = 0;
	// 设置纹理的重复（平铺）模式（如平铺、镜像或钳制），决定在纹理坐标超出 [0,1] 范围时的显示行为，仅对新的绘制命令生效。
	virtual void canvas_texture_set_texture_repeat(RID p_canvas_texture, CanvasItemTextureRepeat p_repeat) = 0;

	/* CANVAS ITEM */

	// 创建画布项
	virtual RID canvas_item_create() = 0;
	// 设置画布项的父节点
	virtual void canvas_item_set_parent(RID p_item, RID p_parent) = 0;

	// 设置画布项的默认纹理滤波模式
	virtual void canvas_item_set_default_texture_filter(RID p_item, CanvasItemTextureFilter p_filter) = 0;
	// 设置画布项的纹理重复模式
	virtual void canvas_item_set_default_texture_repeat(RID p_item, CanvasItemTextureRepeat p_repeat) = 0;

	// 设置画布项的可见性
	virtual void canvas_item_set_visible(RID p_item, bool p_visible) = 0;
	// 为 CanvasItem 指定 2D 光照层掩码，只有与 Light2D 的掩码匹配时才会受到该光源影响，等同于节点层面的 CanvasItem.light_mask 属性
	virtual void canvas_item_set_light_mask(RID p_item, int p_mask) = 0;

	// 设置可见的时候是否更新
	virtual void canvas_item_set_update_when_visible(RID p_item, bool p_update) = 0;

	// 设置画布项的变换
	virtual void canvas_item_set_transform(RID p_item, const Transform2D &p_transform) = 0;
	// 设置画布项的裁剪
	virtual void canvas_item_set_clip(RID p_item, bool p_clip) = 0;
	// 当 p_enable = true 时，启用多通道签名距离场（MSDF）渲染模式，适用于字体渲染或使用 msdfgen 等工具生成的 SDF 图像，从而在任意缩放下保持边缘锐利
	// 在默认模式下，该 CanvasItem 使用普通纹理采样；切换至距离场模式后，渲染管线将基于距离场算法计算像素边界，以获得更精准的边缘抗锯齿效果
	virtual void canvas_item_set_distance_field_mode(RID p_item, bool p_enable) = 0;
	// 当 p_use_custom_rect = true 时，为 CanvasItem 指定一个自定义可见性矩形 p_rect，该矩形用于剔除操作；若为 false，则使用引擎自动计算的包围盒进行剔除
	// 自定义矩形可以显著减少大量 2D 实例的 CPU 剔除开销，常用于动态生成的精灵或 TileMap 等场景中，以提高渲染性能
	virtual void canvas_item_set_custom_rect(RID p_item, bool p_custom_rect, const Rect2 &p_rect = Rect2()) = 0;
	// 对 CanvasItem 及其所有子项施加色彩乘法调制，p_color 的 RGBA 分量会逐像素乘以原始颜色，从而一次性改变整组图元的整体色相或透明度
	virtual void canvas_item_set_modulate(RID p_item, const Color &p_color) = 0;
	// 对 CanvasItem 本身施加色彩乘法调制，而不影响其子项，以实现父项局部高亮或暗化，而子元素保持原色
	// 等价于节点层面的 CanvasItem.self_modulate，在需要单独标记某些父级元素时非常有用
	virtual void canvas_item_set_self_modulate(RID p_item, const Color &p_color) = 0;
	// 为该 CanvasItem 设置渲染可见性层编号 p_visibility_layer，仅当 Viewport 的画布剔除掩码与该层匹配时，该项才会被渲染，支持多视口或分层渲染场景
	// 该机制可精细控制不同 2D 元素在同一或多个视口中的渲染过滤，实现复杂的 UI 层次和特效分离
	virtual void canvas_item_set_visibility_layer(RID p_item, uint32_t p_visibility_layer) = 0;
	// 当 p_enable = true 时，强制让该 CanvasItem 在其父项之后绘制，突破默认“子项在父项之上”的绘制顺序，方便实现底层背景或阴影等效果
	// 相当于节点层面的 CanvasItem.show_behind_parent，可用于制作分层 UI、动态阴影或背景装饰等场景
	virtual void canvas_item_set_draw_behind_parent(RID p_item, bool p_enable) = 0;

	// 用于在 Godot 的 9-切片（nine-patch）绘制中，控制纹理沿某一轴（水平或垂直）如何填充目标区域。
	// 九宫格填充
	enum NinePatchAxisMode {
		NINE_PATCH_STRETCH,		// 在需要的区域对纹理块进行拉伸填充，保证覆盖整个区域，但会导致图块形变。
		NINE_PATCH_TILE,		// 以原始尺寸将纹理块重复平铺，既不会扭曲图块，也可无缝衔接，但要求素材本身能平滑拼接。
		NINE_PATCH_TILE_FIT,	// 同样以原始尺寸平铺，但当剩余空间不能完整容纳一个瓦片时，会对最后一块做适度拉伸，以确保每块图像都能完整显示。
	};

	// 在 p_item 指定的 CanvasItem 上，从 p_from 到 p_to 绘制一条直线，可指定颜色、线宽和是否抗锯齿
	virtual void canvas_item_add_line(RID p_item, const Point2 &p_from, const Point2 &p_to, const Color &p_color, float p_width = -1.0, bool p_antialiased = false) = 0;
	// 绘制一系列按顺序连接的线段，p_points 为顶点列表，p_colors 为每段线段的颜色，同样支持线宽和抗锯齿设置
	virtual void canvas_item_add_polyline(RID p_item, const Vector<Point2> &p_points, const Vector<Color> &p_colors, float p_width = -1.0, bool p_antialiased = false) = 0;
	// 绘制多段不连续的线段集合，每对相邻顶点间绘制一段线，可为每段指定不同颜色，适合同时绘制多条独立折线
	virtual void canvas_item_add_multiline(RID p_item, const Vector<Point2> &p_points, const Vector<Color> &p_colors, float p_width = -1.0, bool p_antialiased = false) = 0;
	// 在 p_rect 区域内绘制一个实心矩形，p_color 控制填充色，支持可选抗锯齿
	virtual void canvas_item_add_rect(RID p_item, const Rect2 &p_rect, const Color &p_color, bool p_antialiased = false) = 0;
	// 以 p_pos 为圆心、p_radius 为半径绘制实心圆，p_color 控制填充色，并可选择是否抗锯齿
	virtual void canvas_item_add_circle(RID p_item, const Point2 &p_pos, float p_radius, const Color &p_color, bool p_antialiased = false) = 0;
	// 将 p_texture 绘制到矩形 p_rect 中，p_tile 控制是否平铺、p_modulate 用于色彩调制、p_transpose 可交换 UV 坐标
	virtual void canvas_item_add_texture_rect(RID p_item, const Rect2 &p_rect, RID p_texture, bool p_tile = false, const Color &p_modulate = Color(1, 1, 1), bool p_transpose = false) = 0;
	// 仅绘制纹理中 p_src_rect 区域映射到目标 p_rect，支持色彩调制、转置和 UV 裁剪 
	virtual void canvas_item_add_texture_rect_region(RID p_item, const Rect2 &p_rect, RID p_texture, const Rect2 &p_src_rect, const Color &p_modulate = Color(1, 1, 1), bool p_transpose = false, bool p_clip_uv = false) = 0;
	// 针对多通道签名距离场（MSDF）纹理添加绘制命令，可指定轮廓宽度 p_outline_size、像素范围 p_px_range 和缩放 p_scale，适合高质量可缩放文本或图形
	virtual void canvas_item_add_msdf_texture_rect_region(RID p_item, const Rect2 &p_rect, RID p_texture, const Rect2 &p_src_rect, const Color &p_modulate = Color(1, 1, 1), int p_outline_size = 0, float p_px_range = 1.0, float p_scale = 1.0) = 0;
	// 使用 LCD SDF 技术绘制文本纹理区域，以获得更清晰的子像素抗锯齿效果
	virtual void canvas_item_add_lcd_texture_rect_region(RID p_item, const Rect2 &p_rect, RID p_texture, const Rect2 &p_src_rect, const Color &p_modulate = Color(1, 1, 1)) = 0;
	// 在 p_rect 区域内绘制九切片图像
	virtual void canvas_item_add_nine_patch(RID p_item, const Rect2 &p_rect, const Rect2 &p_source, RID p_texture, const Vector2 &p_topleft, const Vector2 &p_bottomright, NinePatchAxisMode p_x_axis_mode = NINE_PATCH_STRETCH, NinePatchAxisMode p_y_axis_mode = NINE_PATCH_STRETCH, bool p_draw_center = true, const Color &p_modulate = Color(1, 1, 1)) = 0;
	// 按顶点 p_points、颜色 p_colors、UV 列表 p_uvs 和纹理 p_texture 绘制任意原始几何体，等同于低级三角带或风格化图形命令
	virtual void canvas_item_add_primitive(RID p_item, const Vector<Point2> &p_points, const Vector<Color> &p_colors, const Vector<Point2> &p_uvs, RID p_texture) = 0;
	// 绘制填充多边形，可选地指定 UV 和纹理，实现多边形贴图或纯色填充，常用于自定义形状
	virtual void canvas_item_add_polygon(RID p_item, const Vector<Point2> &p_points, const Vector<Color> &p_colors, const Vector<Point2> &p_uvs = Vector<Point2>(), RID p_texture = RID()) = 0;
	// 以索引列表 p_indices、顶点列表 p_points、颜色 p_colors、UV 列表 p_uvs，以及可选骨骼索引 p_bones 与权重 p_weights 创建带骨骼蒙皮信息的三角形网格，适合骨骼动画
	virtual void canvas_item_add_triangle_array(RID p_item, const Vector<int> &p_indices, const Vector<Point2> &p_points, const Vector<Color> &p_colors, const Vector<Point2> &p_uvs = Vector<Point2>(), const Vector<int> &p_bones = Vector<int>(), const Vector<float> &p_weights = Vector<float>(), RID p_texture = RID(), int p_count = -1) = 0;
	// 将一个 2D 网格资源 p_mesh 绘制到 CanvasItem 上，可附加 p_transform 变换和色彩调制
	virtual void canvas_item_add_mesh(RID p_item, const RID &p_mesh, const Transform2D &p_transform = Transform2D(), const Color &p_modulate = Color(1, 1, 1), RID p_texture = RID()) = 0;
	// 绘制 p_mesh 对应的 MultiMesh 批量实例化渲染，极大提升大量重复图元的性能
	virtual void canvas_item_add_multimesh(RID p_item, RID p_mesh, RID p_texture = RID()) = 0;
	// 在 CanvasItem 上绘制一个 Particles2D 粒子系统实例，配合 p_texture 贴图使用
	virtual void canvas_item_add_particles(RID p_item, RID p_particles, RID p_texture) = 0;
	// 为后续所有绘制命令设置 2D 变换矩阵 p_transform，相当于修改渲染管线内的 extra_matrix uniform
	virtual void canvas_item_add_set_transform(RID p_item, const Transform2D &p_transform) = 0;
	// 控制是否在绘制时忽略 CanvasItem 的当前裁剪区域，true 时命令不受裁剪盒限制，可用于强制覆盖显示 
	virtual void canvas_item_add_clip_ignore(RID p_item, bool p_ignore) = 0;
	// 将帧动画切片命令添加至 CanvasItem，p_animation_length 为总时长，p_slice_begin/p_slice_end 定义当前切片区间，p_offset 为时间偏移，方便按分段播放或循环
	virtual void canvas_item_add_animation_slice(RID p_item, double p_animation_length, double p_slice_begin, double p_slice_end, double p_offset) = 0;

	// 启用后，子 CanvasItem 会根据它们的 Y 坐标进行自动排序，Y 值低的先绘制、Y 值高的后绘制，用于实现类似 “Y 排序” 的视觉深度效果
	virtual void canvas_item_set_sort_children_by_y(RID p_item, bool p_enable) = 0;
	// 手动设置 CanvasItem 的全局 Z 索引，值越大则越晚绘制（越“靠前”）
	// 该属性等价于节点层面的 CanvasItem.z_index，常用于精确控制渲染顺序
	virtual void canvas_item_set_z_index(RID p_item, int p_z) = 0;
	// 当启用时，Z 索引将被视为相对于父项的偏移量；否则，Z 索引为绝对值，覆盖父项的 Z 设置。通常用于在父子层级中实现局部 Z 偏移
	virtual void canvas_item_set_z_as_relative_to_parent(RID p_item, bool p_enable) = 0;
	// 将该 CanvasItem 在绘制后复制到后备缓冲区的指定矩形区域 p_rect，以便后续重用或实现延迟渲染特效
	virtual void canvas_item_set_copy_to_backbuffer(RID p_item, bool p_enable, const Rect2 &p_rect) = 0;

	// 将一个骨骼资源（Skeleton2D）绑定到此 CanvasItem，允许在该画布项上应用骨骼动画
	virtual void canvas_item_attach_skeleton(RID p_item, RID p_skeleton) = 0;

	// 清空此 CanvasItem 上所有已提交的绘制命令，通常在每帧或内容刷新前调用，以避免遗留旧图形
	virtual void canvas_item_clear(RID p_item) = 0;
	// 设置此 CanvasItem 在渲染队列中的绘制顺序索引，值越大则越晚绘制；可与 z_index 配合，实现更细粒度的渲染控制
	virtual void canvas_item_set_draw_index(RID p_item, int p_index) = 0;

	// 为 CanvasItem 指定一个自定义 ShaderMaterial 或 CanvasItemMaterial，用于替换默认的材质渲染管线
	virtual void canvas_item_set_material(RID p_item, RID p_material) = 0;

	// 启用后，该 CanvasItem 将继承父项的材质设置，等同于节点层面的 use_parent_material 属性，用于统一一组项的着色器效果
	virtual void canvas_item_set_use_parent_material(RID p_item, bool p_enable) = 0;

	// 在实例级别为材质着色器动态设置 Uniform 参数 p_name 为 p_value，可实现实时变更效果
	virtual void canvas_item_set_instance_shader_parameter(RID p_item, const StringName &, const Variant &p_value) = 0;
	// 查询当前实例化着色器参数的值 
	virtual Variant canvas_item_get_instance_shader_parameter(RID p_item, const StringName &) const = 0;
	// 获取此着色器参数的默认值，用于重置或做对比
	virtual Variant canvas_item_get_instance_shader_parameter_default_value(RID p_item, const StringName &) const = 0;
	// 列出所有可实例化的着色器参数及其类型信息，便于在运行时遍历或 UI 编辑器中展示 
	virtual void canvas_item_get_instance_shader_parameter_list(RID p_item, List<PropertyInfo> *p_parameters) const = 0;

	// 当启用后，系统将在 p_item 进入或离开指定区域 p_area 时分别调用 p_enter_callable 和 p_exit_callable，可用于触发区域内的显隐逻辑或性能优化（如按需加载）
	virtual void canvas_item_set_visibility_notifier(RID p_item, bool p_enable, const Rect2 &p_area, const Callable &p_enter_callbable, const Callable &p_exit_callable) = 0;

	// CanvasGroup 节点对其子 CanvasItem 节点的渲染与裁剪行为
	enum CanvasGroupMode {
		CANVAS_GROUP_MODE_DISABLED,			// 子节点会正常绘制在父节点之上，且不会受到父节点的裁剪限制
		CANVAS_GROUP_MODE_CLIP_ONLY,		// 父节点仅作为裁剪区域使用，本身不参与绘制；子节点的绘制内容会被裁剪到父节点的可视区域内。
		CANVAS_GROUP_MODE_CLIP_AND_DRAW,	// 父节点既参与自身的绘制，也用作对子节点的裁剪；先绘制父节点，再将子节点裁剪到父节点可视区域。
		CANVAS_GROUP_MODE_TRANSPARENT,
	};

	// 为指定的 CanvasItem 启用高级裁剪/分组绘制模式，子项会先绘制到一个临时缓冲区，再作为单一对象进行混合或裁剪
	/*
	CanvasGroupMode：控制父级对其子项的绘制和裁剪行为，如仅裁剪、裁剪并绘制、透明组合等。
	p_clear_margin：在执行组内绘制前，对裁剪区域按像素进行扩展，以避免边缘 artefact（默认 5px）。
	p_fit_empty：当组内没有子项时，是否仍然渲染一个空的裁剪区域（默认 false）。
	p_fit_margin：在 p_fit_empty = true 时，对该空白区域执行额外的边缘填充或缩放（默认 0px）。
	p_blur_mipmaps：是否在生成或采样 MIP 贴图时启用模糊，以在变换和缩放时保持更平滑的视觉效果（默认 false）。
	*/
	virtual void canvas_item_set_canvas_group_mode(RID p_item, CanvasGroupMode p_mode, float p_clear_margin = 5.0, bool p_fit_empty = false, float p_fit_margin = 0.0, bool p_blur_mipmaps = false) = 0;

	// 开启“调试 CanvasItem 重绘”模式，运行时会在每次触发 redraw 请求时以闪烁的方式可视化这一操作，有助于排查过度重绘或低性能模式下的重绘触发情况
	virtual void canvas_item_set_debug_redraw(bool p_enabled) = 0;
	// 查询当前此调试模式是否已开启；该模式可通过命令行 --debug-canvas-item-redraw 或编辑器菜单 Debug → Debug CanvasItem Redraw 打开与关闭
	virtual bool canvas_item_get_debug_redraw() const = 0;

	// 开启此 CanvasItem 的物理插值，会在物理帧间平滑过渡其位置与变换，避免因高频物理更新而出现跳帧
	virtual void canvas_item_set_interpolated(RID p_item, bool p_interpolated) = 0;
	// 在当前物理时刻取消插值缓存，使该项在下一帧直接应用新位置，适用于需要瞬时移动而非平滑过渡的场景
	virtual void canvas_item_reset_physics_interpolation(RID p_item) = 0;
	// 同时更新该项的“上一个”和“当前” Transform，用于在大世界坐标系（如原点漂移）中平移整个场景时，保持插值连续性，避免视觉抖动
	virtual void canvas_item_transform_physics_interpolation(RID p_item, const Transform2D &p_transform) = 0;

	/* CANVAS LIGHT */
	// 创建画布光源
	virtual RID canvas_light_create() = 0;

	// 两种类型的画布光源：点和方向
	enum CanvasLightMode {
		CANVAS_LIGHT_MODE_POINT,
		CANVAS_LIGHT_MODE_DIRECTIONAL,
	};


	// 设置光源类型
	virtual void canvas_light_set_mode(RID p_light, CanvasLightMode p_mode) = 0;

	// 将光源附加到画布上
	virtual void canvas_light_attach_to_canvas(RID p_light, RID p_canvas) = 0;
	// 启用光源
	virtual void canvas_light_set_enabled(RID p_light, bool p_enabled) = 0;
	// 设置光源的变换
	virtual void canvas_light_set_transform(RID p_light, const Transform2D &p_transform) = 0;
	// 设置光源的颜色
	virtual void canvas_light_set_color(RID p_light, const Color &p_color) = 0;
	// 设置光源相对于画布平面的“高度”，用于模拟光源的垂直距离，影响光照衰减与阴影投射。
	virtual void canvas_light_set_height(RID p_light, float p_height) = 0;
	// 设置光源的能量
	virtual void canvas_light_set_energy(RID p_light, float p_energy) = 0;
	// 限定光源仅对 Z 索引（CanvasItem.z_index）在 [p_min_z, p_max_z] 区间内的项产生影响，实现层级分离的照明
	virtual void canvas_light_set_z_range(RID p_light, int p_min_z, int p_max_z) = 0;
	// 设置光源仅影响可见性层编号在 [p_min_layer, p_max_layer] 范围内的 CanvasItem，可配合多视口或分层渲染场景使用。
	virtual void canvas_light_set_layer_range(RID p_light, int p_min_layer, int p_max_layer) = 0;
	// 为光源指定影响掩码，仅对满足位掩码条件的 CanvasItem 生效，用于精细剔除不需要被照亮的对象。 
	virtual void canvas_light_set_item_cull_mask(RID p_light, int p_mask) = 0;
	// 设定投射阴影时的剔除掩码，仅与此掩码匹配的 CanvasItem 才会被考虑在阴影计算中，优化性能。
	virtual void canvas_light_set_item_shadow_cull_mask(RID p_light, int p_mask) = 0;

	// 仅对方向光模式有效，用于定义光线投射的最大“可见”距离，超出此距离后不再对 CanvasItem 产生照明或阴影
	virtual void canvas_light_set_directional_distance(RID p_light, float p_distance) = 0;

	// 设置自定义光照纹理在屏幕空间的缩放比例，用于细化光照图案或动画效果
	virtual void canvas_light_set_texture_scale(RID p_light, float p_scale) = 0;
	// 关联一个自定义 Texture2D 资源，作为光照投影纹理，支持丰富的遮光和光斑效果。
	virtual void canvas_light_set_texture(RID p_light, RID p_texture) = 0;
	// 调整光照纹理的 UV 偏移，可用于实时滚动或对齐灯光投影图案。
	virtual void canvas_light_set_texture_offset(RID p_light, const Vector2 &p_offset) = 0;

	// 光源的混合模式
	enum CanvasLightBlendMode {
		CANVAS_LIGHT_BLEND_MODE_ADD,		// 将光的颜色值直接加到底色上，产生叠加式增亮效果，是默认且最常用的照明方式
		CANVAS_LIGHT_BLEND_MODE_SUB,		// 将底色减去光的颜色，得到“负光”或局部暗化效果，可用于特殊的反向照明或艺术化效果，虽然不符合物理真实却在一些场景中非常实用
		CANVAS_LIGHT_BLEND_MODE_MIX,		// 根据光源贴图的透明度（Alpha）对光色与场景色进行线性插值，产生更柔和或基于贴图形状的过渡效果，常用于聚光灯、纹理化光晕等需精准控制边缘的场景
	};

	// 设置混合模式
	virtual void canvas_light_set_blend_mode(RID p_light, CanvasLightBlendMode p_mode) = 0;

	enum CanvasLightShadowFilter {
		CANVAS_LIGHT_FILTER_NONE,		// 不对阴影应用任何过滤，直接使用原始深度贴图生成的阴影，这会得到最清晰但最锯齿感严重的阴影
		CANVAS_LIGHT_FILTER_PCF5,		// 使用 5 点采样的 PCF（Percentage-Closer Filtering）算法对阴影进行平滑：对目标像素周围 5 个样本进行深度比较并取平均，从而减少硬边缘和锯齿
		CANVAS_LIGHT_FILTER_PCF13,		// 使用 13 点采样的 PCF 算法，采样更多点以获得更柔和的阴影过渡，代价是更高的 GPU 运算开销
		CANVAS_LIGHT_FILTER_MAX
	};

	// 启用或禁用指定光源的阴影。当 p_enabled = true 时，光源会投射阴影；否则将不再计算或渲染阴影
	virtual void canvas_light_set_shadow_enabled(RID p_light, bool p_enabled) = 0;
	// 设置阴影的过滤（滤波）模式
	virtual void canvas_light_set_shadow_filter(RID p_light, CanvasLightShadowFilter p_filter) = 0;
	// 配置阴影的颜色调制，通过传入 p_color 的 RGBA 分量来改变阴影的色调与透明度，常用于实现非黑色或部分透明的阴影效果 
	virtual void canvas_light_set_shadow_color(RID p_light, const Color &p_color) = 0;
	// 调整阴影的渐变长度或“平滑度”，数值越低边缘越柔和、过渡越宽；数值越高边缘越锐利。该设置影响阴影梯度的计算范围，用于在性能与视觉质量之间做平衡
	virtual void canvas_light_set_shadow_smooth(RID p_light, float p_smooth) = 0;

	// 开启或关闭光源的位置与变换的物理插值。当 p_interpolated = true 时，光源将在物理更新帧与渲染帧之间平滑过渡，减少抖动；否则光源将在渲染时直接采用最新物理位置，出现“跳跃”效果 
	virtual void canvas_light_set_interpolated(RID p_light, bool p_interpolated) = 0;
	// 在当前物理时刻重置光源的插值缓存，使光源在下一帧立即应用新位置，而非从上一位置渐变，常用于需要瞬时移动而非平滑过渡的特效场景
	virtual void canvas_light_reset_physics_interpolation(RID p_light) = 0;
	// 同时更新光源“上一帧”和“当前”存储的变换矩阵，以避免在进行大范围坐标系移动（如原点漂移）时产生插值错位或抖动，保证视觉一致性
	virtual void canvas_light_transform_physics_interpolation(RID p_light, const Transform2D &p_transform) = 0;

	/* CANVAS LIGHT OCCLUDER */

	// 在渲染服务器中创建一个新的光源遮挡器（LightOccluder2D），并返回其 RID；完成使用后应调用 free_rid() 释放该资源
	virtual RID canvas_light_occluder_create() = 0;
	// 将指定的遮挡器附加到某个画布（Canvas）上，若其之前已挂载至其他画布，则会自动移除旧关联
	virtual void canvas_light_occluder_attach_to_canvas(RID p_occluder, RID p_canvas) = 0;
	// 启用或禁用该遮挡器；false 时不会再参与阴影计算和渲染
	virtual void canvas_light_occluder_set_enabled(RID p_occluder, bool p_enabled) = 0;
	// 设置遮挡器的形状
	virtual void canvas_light_occluder_set_polygon(RID p_occluder, RID p_polygon) = 0;
	// 是否将该多边形纳入实时生成的签名距离场（SDF）中，以供自定义着色器使用
	virtual void canvas_light_occluder_set_as_sdf_collision(RID p_occluder, bool p_enable) = 0;
	// 设置变换矩阵
	virtual void canvas_light_occluder_set_transform(RID p_occluder, const Transform2D &p_xform) = 0;
	// 设置光照掩码
	virtual void canvas_light_occluder_set_light_mask(RID p_occluder, int p_mask) = 0;

	// 开启或关闭该遮挡器的物理插值；启用后会在渲染帧与物理帧间平滑过渡位置，避免跳帧抖动
	virtual void canvas_light_occluder_set_interpolated(RID p_occluder, bool p_interpolated) = 0;
	// 在本次物理更新周期内重置插值缓存，使遮挡器瞬时应用新位置，而非从旧位置渐变，适合瞬移场景
	virtual void canvas_light_occluder_reset_physics_interpolation(RID p_occluder) = 0;
	// 同时更新遮挡器的“上一帧”和“当前”存储变换，用于大世界原点漂移等场景，避免插值错位
	virtual void canvas_light_occluder_transform_physics_interpolation(RID p_occluder, const Transform2D &p_transform) = 0;

	/* CANVAS LIGHT OCCLUDER POLYGON */
	// 这段代码提供了一组与2D画布遮挡多边形和阴影贴图相关的底层接口，用于创建和配置 LightOccluder2D 的遮挡多边形、设置其剔除模式、调整阴影纹理大小，以及获取 CanvasItem 的调试矩形信息。

	// 创建一个新的遮挡多边形资源，并返回其RID标识符。
	virtual RID canvas_occluder_polygon_create() = 0;
	// 设置遮挡多边形的顶点列表及是否闭合。
	virtual void canvas_occluder_polygon_set_shape(RID p_occluder_polygon, const Vector<Vector2> &p_shape, bool p_closed) = 0;

	// 定义多边形的剔除模式，可禁用、顺时针剔除或逆时针剔除。
	enum CanvasOccluderPolygonCullMode {
		CANVAS_OCCLUDER_POLYGON_CULL_DISABLED,
		CANVAS_OCCLUDER_POLYGON_CULL_CLOCKWISE,
		CANVAS_OCCLUDER_POLYGON_CULL_COUNTER_CLOCKWISE,
	};

	// 为指定的遮挡多边形设置剔除模式。
	virtual void canvas_occluder_polygon_set_cull_mode(RID p_occluder_polygon, CanvasOccluderPolygonCullMode p_mode) = 0;
	// 配置2D阴影渲染所用阴影纹理贴图的大小（以像素为单位，向上取整到2的幂）。
	virtual void canvas_set_shadow_texture_size(int p_size) = 0;
	
	// 获取画布的大小 
	Rect2 debug_canvas_item_get_rect(RID p_item);
	virtual Rect2 _debug_canvas_item_get_rect(RID p_item) = 0;

	/* GLOBAL SHADER UNIFORMS */

	// 定义了 Godot 渲染服务器（RenderingServer）中全局 shader 参数（即 global uniform）的各种类型，
	// 用于在运行时通过接口（如 RenderingServer.global_shader_parameter_add）向所有 shader 统一传递数据。
	// 它不仅决定了 GPU 端的 uniform 类型（如 bool, vec3, mat4, sampler2D 等），还影响了编辑器中参数的 UI 类型展示（如 Vector3、Color、Texture2D 等）。
	enum GlobalShaderParameterType {
		GLOBAL_VAR_TYPE_BOOL,
		GLOBAL_VAR_TYPE_BVEC2,
		GLOBAL_VAR_TYPE_BVEC3,
		GLOBAL_VAR_TYPE_BVEC4,
		GLOBAL_VAR_TYPE_INT,
		GLOBAL_VAR_TYPE_IVEC2,
		GLOBAL_VAR_TYPE_IVEC3,
		GLOBAL_VAR_TYPE_IVEC4,
		GLOBAL_VAR_TYPE_RECT2I,
		GLOBAL_VAR_TYPE_UINT,
		GLOBAL_VAR_TYPE_UVEC2,
		GLOBAL_VAR_TYPE_UVEC3,
		GLOBAL_VAR_TYPE_UVEC4,
		GLOBAL_VAR_TYPE_FLOAT,
		GLOBAL_VAR_TYPE_VEC2,
		GLOBAL_VAR_TYPE_VEC3,
		GLOBAL_VAR_TYPE_VEC4,
		GLOBAL_VAR_TYPE_COLOR,
		GLOBAL_VAR_TYPE_RECT2,
		GLOBAL_VAR_TYPE_MAT2,
		GLOBAL_VAR_TYPE_MAT3,
		GLOBAL_VAR_TYPE_MAT4,
		GLOBAL_VAR_TYPE_TRANSFORM_2D,
		GLOBAL_VAR_TYPE_TRANSFORM,
		GLOBAL_VAR_TYPE_SAMPLER2D,
		GLOBAL_VAR_TYPE_SAMPLER2DARRAY,
		GLOBAL_VAR_TYPE_SAMPLER3D,
		GLOBAL_VAR_TYPE_SAMPLERCUBE,
		GLOBAL_VAR_TYPE_SAMPLEREXT,
		GLOBAL_VAR_TYPE_MAX
	};

	// 添加全局着色器参数
	virtual void global_shader_parameter_add(const StringName &p_name, GlobalShaderParameterType p_type, const Variant &p_value) = 0;
	// 移除全局着色器参数
	virtual void global_shader_parameter_remove(const StringName &p_name) = 0;
	virtual Vector<StringName> global_shader_parameter_get_list() const = 0;

	// 将名为 name 的全局 uniform 变量设置为 value。此操作会直接写入渲染服务器，而不会强制同步 CPU 与 GPU，因此性能开销极小，适合频繁更新。
	virtual void global_shader_parameter_set(const StringName &p_name, const Variant &p_value) = 0;
	// 与普通 set 不同，此方法在内部生成一个相当于 ShaderGlobalsOverride 节点的覆盖层（override），允许在特定场景或节点范围内临时改变全局 uniform 的值。该覆盖层可在不修改项目设置全局参数的前提下生效。
	virtual void global_shader_parameter_set_override(const StringName &p_name, const Variant &p_value) = 0;

	// 返回当前全局 uniform name 的值
	virtual Variant global_shader_parameter_get(const StringName &p_name) const = 0;
	// 查询全局 uniform name 的类型
	virtual GlobalShaderParameterType global_shader_parameter_get_type(const StringName &p_name) const = 0;

	// 在引擎启动或场景初始化时，从项目设置（project.godot）的 “Shader Globals” 部分加载所有已注册的全局参数，并根据 load_textures 决定是否附带加载其默认纹理。
	virtual void global_shader_parameters_load_settings(bool p_load_textures) = 0;
	// 清除当前所有已注册的全局 uniform 参数，将渲染服务器重置到无全局 uniform 的状态。此操作多用于项目重置或切换大规模渲染配置时。
	virtual void global_shader_parameters_clear() = 0;

	// 将 GlobalShaderParameterType 枚举值映射到底层着色器所使用的数据类型编号（如 GPU API 定义的常量），以便在底层渲染设备中正确创建对应的 uniform 布局和缓冲区。
	static int global_shader_uniform_type_get_shader_datatype(GlobalShaderParameterType p_type);

	/* FREE */
	// 释放资源
	virtual void free(RID p_rid) = 0; // Free RIDs associated with the rendering server.

	/* INTERPOLATION */
	// 启用物理插值
	virtual void set_physics_interpolation_enabled(bool p_enabled) = 0;

	/* EVENT QUEUING */

	// 帧绘制完成后的回调
	virtual void request_frame_drawn_callback(const Callable &p_callable) = 0;

	// 渲染绘制
	virtual void draw(bool p_swap_buffers = true, double frame_step = 0.0) = 0;
	// 同步
	virtual void sync() = 0;
	// 状态检测
	virtual bool has_changed() const = 0;
	// 初始化
	virtual void init();
	// 渲染之后的操作
	virtual void finish() = 0;
	// 每帧的更新
	virtual void tick() = 0;
	// 渲染前的准备
	virtual void pre_draw(bool p_will_draw) = 0;

	/* STATUS INFORMATION */

	// 性能统计指标数
	enum RenderingInfo {
		RENDERING_INFO_TOTAL_OBJECTS_IN_FRAME,		// 一帧的所有对象数
		RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME,	// 一帧的所有图元数
		RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME,	// 一帧的所有drawcall数
		RENDERING_INFO_TEXTURE_MEM_USED,		// 纹理内存使用情况
		RENDERING_INFO_BUFFER_MEM_USED,			// 缓冲内存使用情况。
		RENDERING_INFO_VIDEO_MEM_USED,			// 现存使用量
		RENDERING_INFO_PIPELINE_COMPILATIONS_CANVAS,	// 画布编译次数
		RENDERING_INFO_PIPELINE_COMPILATIONS_MESH,		// 网格编译次数
		RENDERING_INFO_PIPELINE_COMPILATIONS_SURFACE,	// 表面编译次数
		RENDERING_INFO_PIPELINE_COMPILATIONS_DRAW,		// 绘制编译次数
		RENDERING_INFO_PIPELINE_COMPILATIONS_SPECIALIZATION,	// 优化编译次数
		RENDERING_INFO_MAX
	};

	// 返回指定类型的渲染统计数据
	virtual uint64_t get_rendering_info(RenderingInfo p_info) = 0;
	// 显卡名
	virtual String get_video_adapter_name() const = 0;
	// 显卡供应商
	virtual String get_video_adapter_vendor() const = 0;
	// 显卡类型
	virtual RenderingDevice::DeviceType get_video_adapter_type() const = 0;
	// 显卡驱动API版本
	virtual String get_video_adapter_api_version() const = 0;

	// 性能分析
	struct FrameProfileArea {
		String name;		// 阶段名
		double gpu_msec;	// GPU消耗
		double cpu_msec;	// CPU消耗
	};

	// 用于开启或关闭帧级性能剖析
	virtual void set_frame_profiling_enabled(bool p_enable) = 0;
	// 获取帧数据
	virtual Vector<FrameProfileArea> get_frame_profile() = 0;
	// 获取帧编号
	virtual uint64_t get_frame_profile_frame() = 0;

	// 获取帧设置时间
	virtual double get_frame_setup_time_cpu() const = 0;

	// 设置GI使用半分辨率
	virtual void gi_set_use_half_resolution(bool p_enable) = 0;

	/* TESTING */

	virtual RID get_test_cube() = 0;

	virtual RID get_test_texture();
	virtual RID get_white_texture();

	// 在 SDFGI（基于体素的全局光照）调试视图中选定一个探针（probe），通过 position 与 dir 确定该探针在视图中的位置和朝向，便于可视化查看探针采样结果
	virtual void sdfgi_set_debug_probe_select(const Vector3 &p_position, const Vector3 &p_dir) = 0;

	// 创建一个球网格体
	virtual RID make_sphere_mesh(int p_lats, int p_lons, real_t p_radius);

	// 用网格数据填充表面
	virtual void mesh_add_surface_from_mesh_data(RID p_mesh, const Geometry3D::MeshData &p_mesh_data);
	// 用平面添加表面
	virtual void mesh_add_surface_from_planes(RID p_mesh, const Vector<Plane> &p_planes);

	// 设置启动图片
	virtual void set_boot_image(const Ref<Image> &p_image, const Color &p_color, bool p_scale, bool p_use_filter = true) = 0;
	// 获取默认清除色
	virtual Color get_default_clear_color() = 0;
	// 设置默认清除色
	virtual void set_default_clear_color(const Color &p_color) = 0;

#ifndef DISABLE_DEPRECATED
	// Never actually used, should be removed when we can break compatibility.
	enum Features {
		FEATURE_SHADERS,
		FEATURE_MULTITHREADED,
	};
	virtual bool has_feature(Features p_feature) const = 0;
#endif
	// 返回 true 如果操作系统支持指定的功能标签
	virtual bool has_os_feature(const String &p_feature) const = 0;

	// 生成调试线框
	virtual void set_debug_generate_wireframes(bool p_generate) = 0;

	// 设置垂直同步模式
	virtual void call_set_vsync_mode(DisplayServer::VSyncMode p_mode, DisplayServer::WindowID p_window) = 0;

	// 是否是低后端
	virtual bool is_low_end() const = 0;

	// 设置输出GPU分析
	virtual void set_print_gpu_profile(bool p_enable) = 0;

	// 获取最大视口尺寸
	virtual Size2i get_maximum_viewport_size() const = 0;

	// 获取渲染设备
	RenderingDevice *get_rendering_device() const;
	// 创建本地渲染设备
	RenderingDevice *create_local_rendering_device() const;

	// 是否启用了渲染循环
	bool is_render_loop_enabled() const;
	// 设置启用渲染循环
	void set_render_loop_enabled(bool p_enabled);

	// 是否在渲染线程上
	virtual bool is_on_render_thread() = 0;
	// 在渲染线程上调用
	virtual void call_on_render_thread(const Callable &p_callable) = 0;

	// 获取当前渲染驱动名
	String get_current_rendering_driver_name() const;
	// 获取当前渲染方法
	String get_current_rendering_method() const;

#ifdef TOOLS_ENABLED	// Godot 的工具模式（TOOLS_ENABLED）下，用于在编辑器中扩展和定制化功能。

	// 在调用特定方法时动态提供参数补全列表
	virtual void get_argument_options(const StringName &p_function, int p_idx, List<String> *r_options) const override;
#endif

	RenderingServer();
	virtual ~RenderingServer();

#ifdef TOOLS_ENABLED
	typedef void (*SurfaceUpgradeCallback)();
	// 注册一个类型为 void (*)() 的回调函数，当渲染服务器检测到底层“surface”资源需要升级（例如材质或网格格式变更）时，会调用此回调。
	void set_surface_upgrade_callback(SurfaceUpgradeCallback p_callback);
	// 控制在执行“surface”资源自动升级时，是否向用户发出警告提示。传入 true 时，编辑器会在升级过程中弹出提醒；传入 false 则静默完成升级。
	void set_warn_on_surface_upgrade(bool p_warn);
#endif

#ifndef DISABLE_DEPRECATED
	void fix_surface_compatibility(SurfaceData &p_surface, const String &p_path = "");
#endif

private:
	// Binder helpers

	// 创建一个 二维层叠纹理（Texture2DArray／ImageTextureLayered），将传入的多层 Image 拼接为一个分层纹理，并返回对应的 RID，后续可用于材质或着色器采样
	RID _texture_2d_layered_create(const TypedArray<Image> &p_layers, TextureLayeredType p_layered_type);
	// 创建一个 三维纹理（ImageTexture3D），指定像素格式、宽高深和是否生成 Mipmaps，并用 p_data 中的多张 Image 填充各层数据，返回对应的 RID，常用于体积渲染、LUT、密度场等场景 
	RID _texture_3d_create(Image::Format p_format, int p_width, int p_height, int p_depth, bool p_mipmaps, const TypedArray<Image> &p_data);
	// 使用新的三维纹理更新纹理资源
	void _texture_3d_update(RID p_texture, const TypedArray<Image> &p_data);
	// 获取纹理的image资源
	TypedArray<Image> _texture_3d_get(RID p_texture) const;
	// 获取着色器的参数列表
	TypedArray<Dictionary> _shader_get_shader_parameter_list(RID p_shader) const;
	// 用表面数据创建网格资源
	// 表面数据包括：Dictionary 数组（每个字典描述一个面—包括顶点、法线、UV、索引等）
	RID _mesh_create_from_surfaces(const TypedArray<Dictionary> &p_surfaces, int p_blend_shape_count);
	// 将一个表面的数据添加到网格中
	void _mesh_add_surface(RID p_mesh, const Dictionary &p_surface);
	// 获取网格的某个面数据
	Dictionary _mesh_get_surface(RID p_mesh, int p_idx);
	// 获取几何实例的着色器参数列表
	TypedArray<Dictionary> _instance_geometry_get_shader_parameter_list(RID p_instance) const;
	// 获取画布单元实例的着色器参数列表
	TypedArray<Dictionary> _canvas_item_get_instance_shader_parameter_list(RID p_item) const;
	// 烘焙某个实例或者资源
	TypedArray<Image> _bake_render_uv2(RID p_base, const TypedArray<RID> &p_material_overrides, const Size2i &p_image_size);
	// 设置粒子的尾迹绑定姿态
	void _particles_set_trail_bind_poses(RID p_particles, const TypedArray<Transform3D> &p_bind_poses);
#ifdef TOOLS_ENABLED
	SurfaceUpgradeCallback surface_upgrade_callback = nullptr;
	bool warn_on_surface_upgrade = true;
#endif

public:
	/* Debug */
	virtual void save_current_view() = 0;
};

// Make variant understand the enums.
// 让Variant动态类型“理解”这些枚举
VARIANT_ENUM_CAST(RenderingServer::TextureType);
VARIANT_ENUM_CAST(RenderingServer::TextureLayeredType);
VARIANT_ENUM_CAST(RenderingServer::CubeMapLayer);
VARIANT_ENUM_CAST(RenderingServer::PipelineSource);
VARIANT_ENUM_CAST(RenderingServer::ShaderMode);
VARIANT_ENUM_CAST(RenderingServer::ArrayType);
VARIANT_BITFIELD_CAST(RenderingServer::ArrayFormat);
VARIANT_ENUM_CAST(RenderingServer::ArrayCustomFormat);
VARIANT_ENUM_CAST(RenderingServer::PrimitiveType);
VARIANT_ENUM_CAST(RenderingServer::BlendShapeMode);
VARIANT_ENUM_CAST(RenderingServer::MultimeshTransformFormat);
VARIANT_ENUM_CAST(RenderingServer::MultimeshPhysicsInterpolationQuality);
VARIANT_ENUM_CAST(RenderingServer::LightType);
VARIANT_ENUM_CAST(RenderingServer::LightParam);
VARIANT_ENUM_CAST(RenderingServer::LightBakeMode);
VARIANT_ENUM_CAST(RenderingServer::LightOmniShadowMode);
VARIANT_ENUM_CAST(RenderingServer::LightDirectionalShadowMode);
VARIANT_ENUM_CAST(RenderingServer::LightDirectionalSkyMode);
VARIANT_ENUM_CAST(RenderingServer::LightProjectorFilter);
VARIANT_ENUM_CAST(RenderingServer::ReflectionProbeUpdateMode);
VARIANT_ENUM_CAST(RenderingServer::ReflectionProbeAmbientMode);
VARIANT_ENUM_CAST(RenderingServer::VoxelGIQuality);
VARIANT_ENUM_CAST(RenderingServer::DecalTexture);
VARIANT_ENUM_CAST(RenderingServer::DecalFilter);
VARIANT_ENUM_CAST(RenderingServer::ParticlesMode);
VARIANT_ENUM_CAST(RenderingServer::ParticlesTransformAlign);
VARIANT_ENUM_CAST(RenderingServer::ParticlesDrawOrder);
VARIANT_ENUM_CAST(RenderingServer::ParticlesEmitFlags);
VARIANT_ENUM_CAST(RenderingServer::ParticlesCollisionType);
VARIANT_ENUM_CAST(RenderingServer::ParticlesCollisionHeightfieldResolution);
VARIANT_ENUM_CAST(RenderingServer::FogVolumeShape);
VARIANT_ENUM_CAST(RenderingServer::ViewportScaling3DMode);
VARIANT_ENUM_CAST(RenderingServer::ViewportUpdateMode);
VARIANT_ENUM_CAST(RenderingServer::ViewportClearMode);
VARIANT_ENUM_CAST(RenderingServer::ViewportEnvironmentMode);
VARIANT_ENUM_CAST(RenderingServer::ViewportMSAA);
VARIANT_ENUM_CAST(RenderingServer::ViewportAnisotropicFiltering);
VARIANT_ENUM_CAST(RenderingServer::ViewportScreenSpaceAA);
VARIANT_ENUM_CAST(RenderingServer::ViewportRenderInfo);
VARIANT_ENUM_CAST(RenderingServer::ViewportRenderInfoType);
VARIANT_ENUM_CAST(RenderingServer::ViewportDebugDraw);
VARIANT_ENUM_CAST(RenderingServer::ViewportOcclusionCullingBuildQuality);
VARIANT_ENUM_CAST(RenderingServer::ViewportSDFOversize);
VARIANT_ENUM_CAST(RenderingServer::ViewportSDFScale);
VARIANT_ENUM_CAST(RenderingServer::ViewportVRSMode);
VARIANT_ENUM_CAST(RenderingServer::ViewportVRSUpdateMode);
VARIANT_ENUM_CAST(RenderingServer::SkyMode);
VARIANT_ENUM_CAST(RenderingServer::CompositorEffectCallbackType);
VARIANT_ENUM_CAST(RenderingServer::CompositorEffectFlags);
VARIANT_ENUM_CAST(RenderingServer::EnvironmentBG);
VARIANT_ENUM_CAST(RenderingServer::EnvironmentAmbientSource);
VARIANT_ENUM_CAST(RenderingServer::EnvironmentReflectionSource);
VARIANT_ENUM_CAST(RenderingServer::EnvironmentGlowBlendMode);
VARIANT_ENUM_CAST(RenderingServer::EnvironmentFogMode);
VARIANT_ENUM_CAST(RenderingServer::EnvironmentToneMapper);
VARIANT_ENUM_CAST(RenderingServer::EnvironmentSSRRoughnessQuality);
VARIANT_ENUM_CAST(RenderingServer::EnvironmentSSAOQuality);
VARIANT_ENUM_CAST(RenderingServer::EnvironmentSSILQuality);
VARIANT_ENUM_CAST(RenderingServer::EnvironmentSDFGIFramesToConverge);
VARIANT_ENUM_CAST(RenderingServer::EnvironmentSDFGIRayCount);
VARIANT_ENUM_CAST(RenderingServer::EnvironmentSDFGIFramesToUpdateLight);
VARIANT_ENUM_CAST(RenderingServer::EnvironmentSDFGIYScale);
VARIANT_ENUM_CAST(RenderingServer::SubSurfaceScatteringQuality);
VARIANT_ENUM_CAST(RenderingServer::DOFBlurQuality);
VARIANT_ENUM_CAST(RenderingServer::DOFBokehShape);
VARIANT_ENUM_CAST(RenderingServer::ShadowQuality);
VARIANT_ENUM_CAST(RenderingServer::InstanceType);
VARIANT_ENUM_CAST(RenderingServer::InstanceFlags);
VARIANT_ENUM_CAST(RenderingServer::ShadowCastingSetting);
VARIANT_ENUM_CAST(RenderingServer::VisibilityRangeFadeMode);
VARIANT_ENUM_CAST(RenderingServer::NinePatchAxisMode);
VARIANT_ENUM_CAST(RenderingServer::CanvasItemTextureFilter);
VARIANT_ENUM_CAST(RenderingServer::CanvasItemTextureRepeat);
VARIANT_ENUM_CAST(RenderingServer::CanvasGroupMode);
VARIANT_ENUM_CAST(RenderingServer::CanvasLightMode);
VARIANT_ENUM_CAST(RenderingServer::CanvasLightBlendMode);
VARIANT_ENUM_CAST(RenderingServer::CanvasLightShadowFilter);
VARIANT_ENUM_CAST(RenderingServer::CanvasOccluderPolygonCullMode);
VARIANT_ENUM_CAST(RenderingServer::GlobalShaderParameterType);
VARIANT_ENUM_CAST(RenderingServer::RenderingInfo);
VARIANT_ENUM_CAST(RenderingServer::CanvasTextureChannel);
VARIANT_ENUM_CAST(RenderingServer::BakeChannels);

#ifndef DISABLE_DEPRECATED
VARIANT_ENUM_CAST(RenderingServer::Features);
#endif

// Alias to make it easier to use.
// 设置一个别名，让它用起来更容易。
#define RS RenderingServer

#endif // RENDERING_SERVER_H
