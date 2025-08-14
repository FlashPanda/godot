/**************************************************************************/
/*  rendering_server_globals.cpp                                          */
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

#include "rendering_server_globals.h"
#include "core/os/time.h"
#include "core/io/file_access.h"
#include "core/io/dir_access.h"

bool RenderingServerGlobals::threaded = false;

RendererUtilities *RenderingServerGlobals::utilities = nullptr;
RendererLightStorage *RenderingServerGlobals::light_storage = nullptr;
RendererMaterialStorage *RenderingServerGlobals::material_storage = nullptr;
RendererMeshStorage *RenderingServerGlobals::mesh_storage = nullptr;
RendererParticlesStorage *RenderingServerGlobals::particles_storage = nullptr;
RendererTextureStorage *RenderingServerGlobals::texture_storage = nullptr;
RendererGI *RenderingServerGlobals::gi = nullptr;
RendererFog *RenderingServerGlobals::fog = nullptr;
RendererCameraAttributes *RenderingServerGlobals::camera_attributes = nullptr;
RendererCanvasRender *RenderingServerGlobals::canvas_render = nullptr;
RendererCompositor *RenderingServerGlobals::rasterizer = nullptr;

RendererCanvasCull *RenderingServerGlobals::canvas = nullptr;
RendererViewport *RenderingServerGlobals::viewport = nullptr;
RenderingMethod *RenderingServerGlobals::scene = nullptr;

void RenderingServerGlobals::write_log_to_file(String in_string) {
	// 下面的方式可以写文件，这是一种跨平台的方式
	// 文件输出，写入到user://logs/rendering_draw.log
	String log_path = "user://logs/rendering_draw.log";
	// 确保目录存在
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_USERDATA);
	if (da.ptr() && !da->dir_exists("user://logs")) {
		da->make_dir_recursive("user://logs");
	}
	
	// 打开文件并写入
	Ref<FileAccess> f = FileAccess::open(log_path, FileAccess::READ_WRITE);
	if (f.ptr()) {
		f->seek_end();
		String iso_local = Time::get_singleton()->get_datetime_string_from_system(false, true); // 本地 ISO
		// 打印
		CharString u8 = in_string.utf8();
		CharString time_u8 = iso_local.utf8();
		String log_line = vformat("[%s] %s\n", time_u8.get_data(), u8.get_data());
		f->store_string(log_line);
		f->close();
		//memdelete(f);
	}
	else {
		Ref<FileAccess> fa = FileAccess::open(log_path, FileAccess::WRITE);
		if (fa.ptr()) {
			fa->seek_end();
			String iso_local = Time::get_singleton()->get_datetime_string_from_system(false, true); // 本地 ISO
			// 打印
			CharString u8 = in_string.utf8();
			CharString time_u8 = iso_local.utf8();
			String log_line = vformat("[%s] %s\n", time_u8.get_data(), u8.get_data());
			fa->store_string(log_line);
			fa->close();
		}
		else {

		}
	}
}
