/**************************************************************************/
/*  rendering_device.cpp                                                  */
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

#include "rendering_device.h"
#include "rendering_device.compat.inc"

#include "rendering_device_binds.h"
#include "shader_include_db.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "rendering_server_globals.h"

#define FORCE_SEPARATE_PRESENT_QUEUE 0
#define PRINT_FRAMEBUFFER_FORMAT 0

#define ERR_RENDER_THREAD_MSG String("This function (") + String(__func__) + String(") can only be called from the render thread. ")
#define ERR_RENDER_THREAD_GUARD() ERR_FAIL_COND_MSG(render_thread_id != Thread::get_caller_id(), ERR_RENDER_THREAD_MSG);
#define ERR_RENDER_THREAD_GUARD_V(m_ret) ERR_FAIL_COND_V_MSG(render_thread_id != Thread::get_caller_id(), (m_ret), ERR_RENDER_THREAD_MSG);

/**************************/
/**** HELPER FUNCTIONS ****/
/**************************/

static String _get_device_vendor_name(const RenderingContextDriver::Device &p_device) {
	switch (p_device.vendor) {
		case RenderingContextDriver::Vendor::VENDOR_AMD:
			return "AMD";
		case RenderingContextDriver::Vendor::VENDOR_IMGTEC:
			return "ImgTec";
		case RenderingContextDriver::Vendor::VENDOR_APPLE:
			return "Apple";
		case RenderingContextDriver::Vendor::VENDOR_NVIDIA:
			return "NVIDIA";
		case RenderingContextDriver::Vendor::VENDOR_ARM:
			return "ARM";
		case RenderingContextDriver::Vendor::VENDOR_MICROSOFT:
			return "Microsoft";
		case RenderingContextDriver::Vendor::VENDOR_QUALCOMM:
			return "Qualcomm";
		case RenderingContextDriver::Vendor::VENDOR_INTEL:
			return "Intel";
		default:
			return "Unknown";
	}
}

static String _get_device_type_name(const RenderingContextDriver::Device &p_device) {
	switch (p_device.type) {
		case RenderingContextDriver::DEVICE_TYPE_INTEGRATED_GPU:
			return "Integrated";
		case RenderingContextDriver::DEVICE_TYPE_DISCRETE_GPU:
			return "Discrete";
		case RenderingContextDriver::DEVICE_TYPE_VIRTUAL_GPU:
			return "Virtual";
		case RenderingContextDriver::DEVICE_TYPE_CPU:
			return "CPU";
		case RenderingContextDriver::DEVICE_TYPE_OTHER:
		default:
			return "Other";
	}
}

static uint32_t _get_device_type_score(const RenderingContextDriver::Device &p_device) {
	static const bool prefer_integrated = OS::get_singleton()->get_user_prefers_integrated_gpu();
	switch (p_device.type) {
		case RenderingContextDriver::DEVICE_TYPE_INTEGRATED_GPU:
			return prefer_integrated ? 5 : 4;
		case RenderingContextDriver::DEVICE_TYPE_DISCRETE_GPU:
			return prefer_integrated ? 4 : 5;
		case RenderingContextDriver::DEVICE_TYPE_VIRTUAL_GPU:
			return 3;
		case RenderingContextDriver::DEVICE_TYPE_CPU:
			return 2;
		case RenderingContextDriver::DEVICE_TYPE_OTHER:
		default:
			return 1;
	}
}

/**************************/
/**** RENDERING DEVICE ****/
/**************************/

// When true, the command graph will attempt to reorder the rendering commands submitted by the user based on the dependencies detected from
// the commands automatically. This should improve rendering performance in most scenarios at the cost of some extra CPU overhead.
//
// This behavior can be disabled if it's suspected that the graph is not detecting dependencies correctly and more control over the order of
// the commands is desired (e.g. debugging).

// 是否自动优化命令顺序
/*
当为 true 时，命令图会尝试根据检测到的依赖关系，自动对用户提交的渲染命令进行重新排序。
在大多数情况下，这会提高渲染性能，但代价是增加一些额外的 CPU 开销。

如果怀疑命令图没有正确检测依赖关系，并且需要对命令顺序有更多的人工控制（例如用于调试），
则可以禁用此行为。
*/
#define RENDER_GRAPH_REORDER 1

// Synchronization barriers are issued between the graph's levels only with the necessary amount of detail to achieve the correct result. If
// it's suspected that the graph is not doing this correctly, full barriers can be issued instead that will block all types of operations
// between the synchronization levels. This setting will have a very negative impact on performance when enabled, so it's only intended for
// debugging purposes.

/*
同步屏障只会在命令图的层级之间按需发出，以尽量少的细节来保证结果正确。
如果怀疑命令图没有正确处理同步，可以启用“完整屏障”，这样会在同步层级之间阻塞所有类型的操作。

但启用完整屏障会对性能产生非常严重的负面影响，因此仅用于调试目的。
*/
#define RENDER_GRAPH_FULL_BARRIERS 0

// The command graph can automatically issue secondary command buffers and record them on background threads when they reach an arbitrary
// size threshold. This can be very beneficial towards reducing the time the main thread takes to record all the rendering commands. However,
// this setting is not enabled by default as it's been shown to cause some strange issues with certain IHVs that have yet to be understood.

// 是否启用次级命令缓冲区
/*
当命令图达到某个任意大小阈值时，它可以自动生成次级命令缓冲区,并在后台线程中进行录制。

这样做有助于减少主线程在录制所有渲染命令时所花费的时间。
但是，这个设置默认是关闭的，因为在某些 IHV（独立硬件厂商）的驱动上，启用它会出现一些尚未弄清楚的奇怪问题。

PS：也就是说驱动厂商也不知道会有什么奇怪地问题。
*/
#define SECONDARY_COMMAND_BUFFERS_PER_FRAME 0

RenderingDevice *RenderingDevice::singleton = nullptr;

RenderingDevice *RenderingDevice::get_singleton() {
	return singleton;
}

RenderingDevice::ShaderCompileToSPIRVFunction RenderingDevice::compile_to_spirv_function = nullptr;
RenderingDevice::ShaderCacheFunction RenderingDevice::cache_function = nullptr;
RenderingDevice::ShaderSPIRVGetCacheKeyFunction RenderingDevice::get_spirv_cache_key_function = nullptr;

/***************************/
/**** ID INFRASTRUCTURE ****/
/***************************/

void RenderingDevice::_add_dependency(RID p_id, RID p_depends_on) {
	_THREAD_SAFE_METHOD_

	HashSet<RID> *set = dependency_map.getptr(p_depends_on);
	if (set == nullptr) {
		set = &dependency_map.insert(p_depends_on, HashSet<RID>())->value;
	}
	set->insert(p_id);

	set = reverse_dependency_map.getptr(p_id);
	if (set == nullptr) {
		set = &reverse_dependency_map.insert(p_id, HashSet<RID>())->value;
	}
	set->insert(p_depends_on);
}

void RenderingDevice::_free_dependencies(RID p_id) {
	_THREAD_SAFE_METHOD_

	// Direct dependencies must be freed.

	HashMap<RID, HashSet<RID>>::Iterator E = dependency_map.find(p_id);
	if (E) {
		while (E->value.size()) {
			free(*E->value.begin());
		}
		dependency_map.remove(E);
	}

	// Reverse dependencies must be unreferenced.
	E = reverse_dependency_map.find(p_id);

	if (E) {
		for (const RID &F : E->value) {
			HashMap<RID, HashSet<RID>>::Iterator G = dependency_map.find(F);
			ERR_CONTINUE(!G);
			ERR_CONTINUE(!G->value.has(p_id));
			G->value.erase(p_id);
		}

		reverse_dependency_map.remove(E);
	}
}

/*******************************/
/**** SHADER INFRASTRUCTURE ****/
/*******************************/

void RenderingDevice::shader_set_compile_to_spirv_function(ShaderCompileToSPIRVFunction p_function) {
	compile_to_spirv_function = p_function;
}

void RenderingDevice::shader_set_spirv_cache_function(ShaderCacheFunction p_function) {
	cache_function = p_function;
}

void RenderingDevice::shader_set_get_cache_key_function(ShaderSPIRVGetCacheKeyFunction p_function) {
	get_spirv_cache_key_function = p_function;
}

Vector<uint8_t> RenderingDevice::shader_compile_spirv_from_source(ShaderStage p_stage, const String &p_source_code, ShaderLanguage p_language, String *r_error, bool p_allow_cache) {
	if (p_allow_cache && cache_function) {
		Vector<uint8_t> cache = cache_function(p_stage, p_source_code, p_language);
		if (cache.size()) {
			return cache;
		}
	}

	ERR_FAIL_NULL_V(compile_to_spirv_function, Vector<uint8_t>());

	return compile_to_spirv_function(p_stage, ShaderIncludeDB::parse_include_files(p_source_code), p_language, r_error, this);
}

String RenderingDevice::shader_get_spirv_cache_key() const {
	if (get_spirv_cache_key_function) {
		return get_spirv_cache_key_function(this);
	}
	return String();
}

RID RenderingDevice::shader_create_from_spirv(const Vector<ShaderStageSPIRVData> &p_spirv, const String &p_shader_name) {
	Vector<uint8_t> bytecode = shader_compile_binary_from_spirv(p_spirv, p_shader_name);
	ERR_FAIL_COND_V(bytecode.is_empty(), RID());
	return shader_create_from_bytecode(bytecode);
}

/***************************/
/**** BUFFER MANAGEMENT ****/
/***************************/

RenderingDevice::Buffer *RenderingDevice::_get_buffer_from_owner(RID p_buffer) {
	Buffer *buffer = nullptr;
	if (vertex_buffer_owner.owns(p_buffer)) {
		buffer = vertex_buffer_owner.get_or_null(p_buffer);
	} else if (index_buffer_owner.owns(p_buffer)) {
		buffer = index_buffer_owner.get_or_null(p_buffer);
	} else if (uniform_buffer_owner.owns(p_buffer)) {
		buffer = uniform_buffer_owner.get_or_null(p_buffer);
	} else if (texture_buffer_owner.owns(p_buffer)) {
		DEV_ASSERT(false && "FIXME: Broken.");
		//buffer = texture_buffer_owner.get_or_null(p_buffer)->buffer;
	} else if (storage_buffer_owner.owns(p_buffer)) {
		buffer = storage_buffer_owner.get_or_null(p_buffer);
	}
	return buffer;
}

Error RenderingDevice::_buffer_initialize(Buffer *p_buffer, const uint8_t *p_data, size_t p_data_size, uint32_t p_required_align) {
	uint32_t transfer_worker_offset;
	TransferWorker *transfer_worker = _acquire_transfer_worker(p_data_size, p_required_align, transfer_worker_offset);
	p_buffer->transfer_worker_index = transfer_worker->index;

	{
		MutexLock lock(transfer_worker->operations_mutex);
		p_buffer->transfer_worker_operation = ++transfer_worker->operations_counter;
	}

	// Copy to the worker's staging buffer.
	uint8_t *data_ptr = driver->buffer_map(transfer_worker->staging_buffer);
	ERR_FAIL_NULL_V(data_ptr, ERR_CANT_CREATE);

	memcpy(data_ptr + transfer_worker_offset, p_data, p_data_size);
	driver->buffer_unmap(transfer_worker->staging_buffer);

	// Copy from the staging buffer to the real buffer.
	RDD::BufferCopyRegion region;
	region.src_offset = transfer_worker_offset;
	region.dst_offset = 0;
	region.size = p_data_size;
	driver->command_copy_buffer(transfer_worker->command_buffer, transfer_worker->staging_buffer, p_buffer->driver_id, region);

	_release_transfer_worker(transfer_worker);

	return OK;
}

Error RenderingDevice::_insert_staging_block(StagingBuffers &p_staging_buffers) {
	StagingBufferBlock block;

	block.driver_id = driver->buffer_create(p_staging_buffers.block_size, p_staging_buffers.usage_bits, RDD::MEMORY_ALLOCATION_TYPE_CPU);
	ERR_FAIL_COND_V(!block.driver_id, ERR_CANT_CREATE);

	block.frame_used = 0;
	block.fill_amount = 0;

	p_staging_buffers.blocks.insert(p_staging_buffers.current, block);
	return OK;
}

Error RenderingDevice::_staging_buffer_allocate(StagingBuffers &p_staging_buffers, uint32_t p_amount, uint32_t p_required_align, uint32_t &r_alloc_offset, uint32_t &r_alloc_size, StagingRequiredAction &r_required_action, bool p_can_segment) {
	// Determine a block to use.
	// 决定要使用的一个区块

	r_alloc_size = p_amount;	// 需要的尺寸大小
	r_required_action = STAGING_REQUIRED_ACTION_NONE;	// 不需要额外操作

	while (true) {
		r_alloc_offset = 0;	// 偏移量

		// See if we can use current block.
		// 确定是否可以用当前的block
		if (p_staging_buffers.blocks[p_staging_buffers.current].frame_used == frames_drawn) {	// 如果这帧在使用这个缓冲
			// We used this block this frame, let's see if there is still room.
			// 我们可以使用这个区块了，看看还有没有空间余留

			uint32_t write_from = p_staging_buffers.blocks[p_staging_buffers.current].fill_amount;

			// 对齐偏移
			{
				uint32_t align_remainder = write_from % p_required_align;
				if (align_remainder != 0) {
					write_from += p_required_align - align_remainder;
				}
			}

			// block当前剩余量
			int32_t available_bytes = int32_t(p_staging_buffers.block_size) - int32_t(write_from);

			if ((int32_t)p_amount < available_bytes) {
				// All is good, we should be ok, all will fit.
				// 需要的量小于剩余量，那么一切完美
				r_alloc_offset = write_from;
			} else if (p_can_segment && available_bytes >= (int32_t)p_required_align) {
				// 如果可以分段，然后余量大于对齐量
				// Ok all won't fit but at least we can fit a chunkie.
				// All is good, update what needs to be written to.
				r_alloc_offset = write_from;
				r_alloc_size = available_bytes - (available_bytes % p_required_align);

			} else {
				// Can't fit it into this buffer.
				// Will need to try next buffer.
				// 没法进行填充了，我们需要再找一个。再找一个的话就是重新进入那个判断循环

				p_staging_buffers.current = (p_staging_buffers.current + 1) % p_staging_buffers.blocks.size();

				// Before doing anything, though, let's check that we didn't manage to fill all blocks.
				// Possible in a single frame.
				// 不过，在做任何事情之前，我们先检查一下是否已经把所有的块都填满了。这在单帧内是有可能发生的
				if (p_staging_buffers.blocks[p_staging_buffers.current].frame_used == frames_drawn) {
					// Guess we did.. ok, let's see if we can insert a new block.
					// 好吧，需要新插入一个区块了

					// 总体暂存尺寸是否达到，如果没有就可以新插入一个
					if ((uint64_t)p_staging_buffers.blocks.size() * p_staging_buffers.block_size < p_staging_buffers.max_size) {
						// We can, so we are safe.
						Error err = _insert_staging_block(p_staging_buffers);
						if (err) {
							return err;
						}
						// Claim for this frame.
						// 标记这个东西正在被这帧使用了
						p_staging_buffers.blocks.write[p_staging_buffers.current].frame_used = frames_drawn;
					} else {
						// Ok, worst case scenario, all the staging buffers belong to this frame
						// and this frame is not even done.
						// If this is the main thread, it means the user is likely loading a lot of resources at once,.
						// Otherwise, the thread should just be blocked until the next frame (currently unimplemented).
						// 好了，现在碰到了一个最糟糕的情况，所有的中转block都已经被用了，那么如果这是主线程，就意味着用户在一次性加载大量的资源
						// 否则，就只能等下一帧了。
						r_required_action = STAGING_REQUIRED_ACTION_FLUSH_AND_STALL_ALL;
					}

				} else {
					// 这个区块不是当前帧用的，没办法，等下一block试试。
					// Not from current frame, so continue and try again.
					continue;
				}
			}

		} else if (p_staging_buffers.blocks[p_staging_buffers.current].frame_used <= frames_drawn - frames.size()) {
			// This is an old block, which was already processed, let's reuse.
			// 这已经是一个老的区块，并且被使用过，我们要重用它
			p_staging_buffers.blocks.write[p_staging_buffers.current].frame_used = frames_drawn;
			p_staging_buffers.blocks.write[p_staging_buffers.current].fill_amount = 0;
		} else {
			// This block may still be in use, let's not touch it unless we have to, so.. can we create a new one?
			// 这个区块正在被使用，我们最好别去动它，所以...我们再新建一个？
			if ((uint64_t)p_staging_buffers.blocks.size() * p_staging_buffers.block_size < p_staging_buffers.max_size) {
				// We are still allowed to create a new block, so let's do that and insert it for current pos.
				// 我们就创建一个新的区块
				Error err = _insert_staging_block(p_staging_buffers);
				if (err) {
					return err;
				}
				// Claim for this frame.
				// 声明是这帧用的
				p_staging_buffers.blocks.write[p_staging_buffers.current].frame_used = frames_drawn;
			} else {
				// Oops, we are out of room and we can't create more.
				// Let's flush older frames.
				// The logic here is that if a game is loading a lot of data from the main thread, it will need to be stalled anyway.
				// If loading from a separate thread, we can block that thread until next frame when more room is made (not currently implemented, though).
				// 同样是没有空间可以用了，，就只能阻塞等待
				r_required_action = STAGING_REQUIRED_ACTION_STALL_PREVIOUS;
			}
		}

		// All was good, break.
		break;
	}

	p_staging_buffers.used = true;

	return OK;
}

void RenderingDevice::_staging_buffer_execute_required_action(StagingBuffers &p_staging_buffers, StagingRequiredAction p_required_action) {
	switch (p_required_action) {
		case STAGING_REQUIRED_ACTION_NONE: {
			// Do nothing.
		} break;
		case STAGING_REQUIRED_ACTION_FLUSH_AND_STALL_ALL: {
			_flush_and_stall_for_all_frames();

			// Clear the whole staging buffer.
			for (int i = 0; i < p_staging_buffers.blocks.size(); i++) {
				p_staging_buffers.blocks.write[i].frame_used = 0;
				p_staging_buffers.blocks.write[i].fill_amount = 0;
			}

			// Claim for current frame.
			p_staging_buffers.blocks.write[p_staging_buffers.current].frame_used = frames_drawn;
		} break;
		case STAGING_REQUIRED_ACTION_STALL_PREVIOUS: {
			_stall_for_previous_frames();

			for (int i = 0; i < p_staging_buffers.blocks.size(); i++) {
				// Clear all blocks but the ones from this frame.
				int block_idx = (i + p_staging_buffers.current) % p_staging_buffers.blocks.size();
				if (p_staging_buffers.blocks[block_idx].frame_used == frames_drawn) {
					break; // Ok, we reached something from this frame, abort.
				}

				p_staging_buffers.blocks.write[block_idx].frame_used = 0;
				p_staging_buffers.blocks.write[block_idx].fill_amount = 0;
			}

			// Claim for current frame.
			p_staging_buffers.blocks.write[p_staging_buffers.current].frame_used = frames_drawn;
		} break;
		default: {
			DEV_ASSERT(false && "Unknown required action.");
		} break;
	}
}

Error RenderingDevice::buffer_copy(RID p_src_buffer, RID p_dst_buffer, uint32_t p_src_offset, uint32_t p_dst_offset, uint32_t p_size) {
	ERR_RENDER_THREAD_GUARD_V(ERR_UNAVAILABLE);

	ERR_FAIL_COND_V_MSG(draw_list, ERR_INVALID_PARAMETER,
			"Copying buffers is forbidden during creation of a draw list");
	ERR_FAIL_COND_V_MSG(compute_list, ERR_INVALID_PARAMETER,
			"Copying buffers is forbidden during creation of a compute list");

	Buffer *src_buffer = _get_buffer_from_owner(p_src_buffer);
	if (!src_buffer) {
		ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "Source buffer argument is not a valid buffer of any type.");
	}

	Buffer *dst_buffer = _get_buffer_from_owner(p_dst_buffer);
	if (!dst_buffer) {
		ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "Destination buffer argument is not a valid buffer of any type.");
	}

	// Validate the copy's dimensions for both buffers.
	ERR_FAIL_COND_V_MSG((p_size + p_src_offset) > src_buffer->size, ERR_INVALID_PARAMETER, "Size is larger than the source buffer.");
	ERR_FAIL_COND_V_MSG((p_size + p_dst_offset) > dst_buffer->size, ERR_INVALID_PARAMETER, "Size is larger than the destination buffer.");

	_check_transfer_worker_buffer(src_buffer);
	_check_transfer_worker_buffer(dst_buffer);

	// Perform the copy.
	RDD::BufferCopyRegion region;
	region.src_offset = p_src_offset;
	region.dst_offset = p_dst_offset;
	region.size = p_size;

	if (_buffer_make_mutable(dst_buffer, p_dst_buffer)) {
		// The destination buffer must be mutable to be used as a copy destination.
		draw_graph.add_synchronization();
	}

	draw_graph.add_buffer_copy(src_buffer->driver_id, src_buffer->draw_tracker, dst_buffer->driver_id, dst_buffer->draw_tracker, region);

	return OK;
}

Error RenderingDevice::buffer_update(RID p_buffer, uint32_t p_offset, uint32_t p_size, const void *p_data)
{
	ERR_RENDER_THREAD_GUARD_V(ERR_UNAVAILABLE);

	copy_bytes_count += p_size;

	ERR_FAIL_COND_V_MSG(draw_list, ERR_INVALID_PARAMETER,
			"Updating buffers is forbidden during creation of a draw list");
	ERR_FAIL_COND_V_MSG(compute_list, ERR_INVALID_PARAMETER,
			"Updating buffers is forbidden during creation of a compute list");

	Buffer *buffer = _get_buffer_from_owner(p_buffer);
	ERR_FAIL_NULL_V_MSG(buffer, ERR_INVALID_PARAMETER, "Buffer argument is not a valid buffer of any type.");
	ERR_FAIL_COND_V_MSG(p_offset + p_size > buffer->size, ERR_INVALID_PARAMETER, "Attempted to write buffer (" + itos((p_offset + p_size) - buffer->size) + " bytes) past the end.");

	// 检查是否正在被传输线程使用
	_check_transfer_worker_buffer(buffer);

	// Submitting may get chunked for various reasons, so convert this to a task.
	size_t to_submit = p_size;		// 需要上传的数据总量
	size_t submit_from = 0;			// 本次上传的数据在整体数据中的偏移

	// 线程本地的“本批次要做的 GPU 拷贝命令列表”。用 thread_local 减少频繁分配
	thread_local LocalVector<RDG::RecordedBufferCopy> command_buffer_copies_vector;
	command_buffer_copies_vector.clear();

	const uint8_t *src_data = reinterpret_cast<const uint8_t *>(p_data);	// 转为字节流指针
	const uint32_t required_align = 32;	// 32字节对齐
	while (to_submit > 0) {
		uint32_t block_write_offset;
		uint32_t block_write_amount;
		StagingRequiredAction required_action;

		Error err = _staging_buffer_allocate(upload_staging_buffers, MIN(to_submit, upload_staging_buffers.block_size), required_align, block_write_offset, block_write_amount, required_action);
		if (err) {
			return err;
		}

		if (!command_buffer_copies_vector.is_empty() && required_action == STAGING_REQUIRED_ACTION_FLUSH_AND_STALL_ALL) {
			if (_buffer_make_mutable(buffer, p_buffer)) {
				// The buffer must be mutable to be used as a copy destination.
				draw_graph.add_synchronization();
			}

			draw_graph.add_buffer_update(buffer->driver_id, buffer->draw_tracker, command_buffer_copies_vector);
			command_buffer_copies_vector.clear();
		}

		_staging_buffer_execute_required_action(upload_staging_buffers, required_action);

		// Map staging buffer (It's CPU and coherent).
		uint8_t *data_ptr = driver->buffer_map(upload_staging_buffers.blocks[upload_staging_buffers.current].driver_id);
		ERR_FAIL_NULL_V(data_ptr, ERR_CANT_CREATE);

		// Copy to staging buffer.
		memcpy(data_ptr + block_write_offset, src_data + submit_from, block_write_amount);

		// Unmap.
		driver->buffer_unmap(upload_staging_buffers.blocks[upload_staging_buffers.current].driver_id);

		// Insert a command to copy this.
		RDD::BufferCopyRegion region;
		region.src_offset = block_write_offset;
		region.dst_offset = submit_from + p_offset;
		region.size = block_write_amount;

		RDG::RecordedBufferCopy buffer_copy;
		buffer_copy.source = upload_staging_buffers.blocks[upload_staging_buffers.current].driver_id;
		buffer_copy.region = region;
		command_buffer_copies_vector.push_back(buffer_copy);

		upload_staging_buffers.blocks.write[upload_staging_buffers.current].fill_amount = block_write_offset + block_write_amount;

		to_submit -= block_write_amount;
		submit_from += block_write_amount;
	}

	if (!command_buffer_copies_vector.is_empty()) {
		if (_buffer_make_mutable(buffer, p_buffer)) {
			// The buffer must be mutable to be used as a copy destination.
			draw_graph.add_synchronization();
		}

		draw_graph.add_buffer_update(buffer->driver_id, buffer->draw_tracker, command_buffer_copies_vector);
	}

	gpu_copy_count++;

	return OK;
}

Error RenderingDevice::driver_callback_add(RDD::DriverCallback p_callback, void *p_userdata, VectorView<CallbackResource> p_resources) {
	ERR_RENDER_THREAD_GUARD_V(ERR_UNAVAILABLE);

	ERR_FAIL_COND_V_MSG(draw_list, ERR_INVALID_PARAMETER,
			"Driver callback is forbidden during creation of a draw list");
	ERR_FAIL_COND_V_MSG(compute_list, ERR_INVALID_PARAMETER,
			"Driver callback is forbidden during creation of a compute list");

	thread_local LocalVector<RDG::ResourceTracker *> trackers;
	thread_local LocalVector<RDG::ResourceUsage> usages;

	uint32_t resource_count = p_resources.size();
	trackers.resize(resource_count);
	usages.resize(resource_count);

	if (resource_count > 0) {
		for (uint32_t i = 0; i < p_resources.size(); i++) {
			const CallbackResource &cr = p_resources[i];
			switch (cr.type) {
				case CALLBACK_RESOURCE_TYPE_BUFFER: {
					Buffer *buffer = _get_buffer_from_owner(cr.rid);
					if (!buffer) {
						ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, vformat("Argument %d is not a valid buffer of any type.", i));
					}
					if (_buffer_make_mutable(buffer, cr.rid)) {
						draw_graph.add_synchronization();
					}
					trackers[i] = buffer->draw_tracker;
					usages[i] = (RDG::ResourceUsage)cr.usage;
				} break;
				case CALLBACK_RESOURCE_TYPE_TEXTURE: {
					Texture *texture = texture_owner.get_or_null(cr.rid);
					if (!texture) {
						ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, vformat("Argument %d is not a valid texture.", i));
					}
					if (_texture_make_mutable(texture, cr.rid)) {
						draw_graph.add_synchronization();
					}
					trackers[i] = texture->draw_tracker;
					usages[i] = (RDG::ResourceUsage)cr.usage;
				} break;
				default: {
					CRASH_NOW_MSG("Invalid callback resource type.");
				} break;
			}
		}
	}

	draw_graph.add_driver_callback(p_callback, p_userdata, trackers, usages);

	return OK;
}

String RenderingDevice::get_perf_report() const {
	return perf_report_text;
}

void RenderingDevice::update_perf_report() {
	perf_report_text = "";
	perf_report_text += " gpu:" + String::num_int64(gpu_copy_count);
	perf_report_text += " bytes:" + String::num_int64(copy_bytes_count);

	perf_report_text += " lazily alloc:" + String::num_int64(driver->get_lazily_memory_used());

	gpu_copy_count = 0;
	copy_bytes_count = 0;
}

Error RenderingDevice::buffer_clear(RID p_buffer, uint32_t p_offset, uint32_t p_size) {
	ERR_RENDER_THREAD_GUARD_V(ERR_UNAVAILABLE);

	ERR_FAIL_COND_V_MSG((p_size % 4) != 0, ERR_INVALID_PARAMETER,
			"Size must be a multiple of four");
	ERR_FAIL_COND_V_MSG(draw_list, ERR_INVALID_PARAMETER,
			"Updating buffers in is forbidden during creation of a draw list");
	ERR_FAIL_COND_V_MSG(compute_list, ERR_INVALID_PARAMETER,
			"Updating buffers is forbidden during creation of a compute list");

	Buffer *buffer = _get_buffer_from_owner(p_buffer);
	if (!buffer) {
		ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "Buffer argument is not a valid buffer of any type.");
	}

	ERR_FAIL_COND_V_MSG(p_offset + p_size > buffer->size, ERR_INVALID_PARAMETER,
			"Attempted to write buffer (" + itos((p_offset + p_size) - buffer->size) + " bytes) past the end.");

	_check_transfer_worker_buffer(buffer);

	if (_buffer_make_mutable(buffer, p_buffer)) {
		// The destination buffer must be mutable to be used as a clear destination.
		draw_graph.add_synchronization();
	}

	draw_graph.add_buffer_clear(buffer->driver_id, buffer->draw_tracker, p_offset, p_size);

	return OK;
}

Vector<uint8_t> RenderingDevice::buffer_get_data(RID p_buffer, uint32_t p_offset, uint32_t p_size) {
	ERR_RENDER_THREAD_GUARD_V(Vector<uint8_t>());

	Buffer *buffer = _get_buffer_from_owner(p_buffer);
	if (!buffer) {
		ERR_FAIL_V_MSG(Vector<uint8_t>(), "Buffer is either invalid or this type of buffer can't be retrieved.");
	}

	// Size of buffer to retrieve.
	if (!p_size) {
		p_size = buffer->size;
	} else {
		ERR_FAIL_COND_V_MSG(p_size + p_offset > buffer->size, Vector<uint8_t>(),
				"Size is larger than the buffer.");
	}

	_check_transfer_worker_buffer(buffer);

	RDD::BufferID tmp_buffer = driver->buffer_create(buffer->size, RDD::BUFFER_USAGE_TRANSFER_TO_BIT, RDD::MEMORY_ALLOCATION_TYPE_CPU);
	ERR_FAIL_COND_V(!tmp_buffer, Vector<uint8_t>());

	RDD::BufferCopyRegion region;
	region.src_offset = p_offset;
	region.size = p_size;

	draw_graph.add_buffer_get_data(buffer->driver_id, buffer->draw_tracker, tmp_buffer, region);

	// Flush everything so memory can be safely mapped.
	_flush_and_stall_for_all_frames();

	uint8_t *buffer_mem = driver->buffer_map(tmp_buffer);
	ERR_FAIL_NULL_V(buffer_mem, Vector<uint8_t>());

	Vector<uint8_t> buffer_data;
	{
		buffer_data.resize(p_size);
		uint8_t *w = buffer_data.ptrw();
		memcpy(w, buffer_mem, p_size);
	}

	driver->buffer_unmap(tmp_buffer);

	driver->buffer_free(tmp_buffer);

	return buffer_data;
}

Error RenderingDevice::buffer_get_data_async(RID p_buffer, const Callable &p_callback, uint32_t p_offset, uint32_t p_size) {
	ERR_RENDER_THREAD_GUARD_V(ERR_UNAVAILABLE);

	Buffer *buffer = _get_buffer_from_owner(p_buffer);
	if (buffer == nullptr) {
		ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "Buffer is either invalid or this type of buffer can't be retrieved.");
	}

	if (p_size == 0) {
		p_size = buffer->size;
	}

	ERR_FAIL_COND_V_MSG(p_size + p_offset > buffer->size, ERR_INVALID_PARAMETER, "Size is larger than the buffer.");
	ERR_FAIL_COND_V_MSG(!p_callback.is_valid(), ERR_INVALID_PARAMETER, "Callback must be valid.");

	_check_transfer_worker_buffer(buffer);

	BufferGetDataRequest get_data_request;
	uint32_t flushed_copies = 0;
	get_data_request.callback = p_callback;
	get_data_request.frame_local_index = frames[frame].download_buffer_copy_regions.size();
	get_data_request.size = p_size;

	const uint32_t required_align = 32;
	uint32_t block_write_offset;
	uint32_t block_write_amount;
	StagingRequiredAction required_action;
	uint32_t to_submit = p_size;
	uint32_t submit_from = 0;
	while (to_submit > 0) {
		Error err = _staging_buffer_allocate(download_staging_buffers, MIN(to_submit, download_staging_buffers.block_size), required_align, block_write_offset, block_write_amount, required_action);
		if (err) {
			return err;
		}

		if ((get_data_request.frame_local_count > 0) && required_action == STAGING_REQUIRED_ACTION_FLUSH_AND_STALL_ALL) {
			if (_buffer_make_mutable(buffer, p_buffer)) {
				// The buffer must be mutable to be used as a copy source.
				draw_graph.add_synchronization();
			}

			for (uint32_t i = flushed_copies; i < get_data_request.frame_local_count; i++) {
				uint32_t local_index = get_data_request.frame_local_index + i;
				draw_graph.add_buffer_get_data(buffer->driver_id, buffer->draw_tracker, frames[frame].download_buffer_staging_buffers[local_index], frames[frame].download_buffer_copy_regions[local_index]);
			}

			flushed_copies = get_data_request.frame_local_count;
		}

		_staging_buffer_execute_required_action(download_staging_buffers, required_action);

		RDD::BufferCopyRegion region;
		region.src_offset = submit_from + p_offset;
		region.dst_offset = block_write_offset;
		region.size = block_write_amount;

		frames[frame].download_buffer_staging_buffers.push_back(download_staging_buffers.blocks[download_staging_buffers.current].driver_id);
		frames[frame].download_buffer_copy_regions.push_back(region);
		get_data_request.frame_local_count++;

		download_staging_buffers.blocks.write[download_staging_buffers.current].fill_amount = block_write_offset + block_write_amount;

		to_submit -= block_write_amount;
		submit_from += block_write_amount;
	}

	if (get_data_request.frame_local_count > 0) {
		if (_buffer_make_mutable(buffer, p_buffer)) {
			// The buffer must be mutable to be used as a copy source.
			draw_graph.add_synchronization();
		}

		for (uint32_t i = flushed_copies; i < get_data_request.frame_local_count; i++) {
			uint32_t local_index = get_data_request.frame_local_index + i;
			draw_graph.add_buffer_get_data(buffer->driver_id, buffer->draw_tracker, frames[frame].download_buffer_staging_buffers[local_index], frames[frame].download_buffer_copy_regions[local_index]);
		}

		frames[frame].download_buffer_get_data_requests.push_back(get_data_request);
	}

	return OK;
}

RID RenderingDevice::storage_buffer_create(uint32_t p_size_bytes, const Vector<uint8_t> &p_data, BitField<StorageBufferUsage> p_usage) {
	ERR_FAIL_COND_V(p_data.size() && (uint32_t)p_data.size() != p_size_bytes, RID());

	Buffer buffer;
	buffer.size = p_size_bytes;
	buffer.usage = (RDD::BUFFER_USAGE_TRANSFER_FROM_BIT | RDD::BUFFER_USAGE_TRANSFER_TO_BIT | RDD::BUFFER_USAGE_STORAGE_BIT);
	if (p_usage.has_flag(STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT)) {
		buffer.usage.set_flag(RDD::BUFFER_USAGE_INDIRECT_BIT);
	}
	buffer.driver_id = driver->buffer_create(buffer.size, buffer.usage, RDD::MEMORY_ALLOCATION_TYPE_GPU);
	ERR_FAIL_COND_V(!buffer.driver_id, RID());

	// Storage buffers are assumed to be mutable.
	buffer.draw_tracker = RDG::resource_tracker_create();
	buffer.draw_tracker->buffer_driver_id = buffer.driver_id;

	if (p_data.size()) {
		_buffer_initialize(&buffer, p_data.ptr(), p_data.size());
	}

	_THREAD_SAFE_LOCK_
	buffer_memory += buffer.size;
	_THREAD_SAFE_UNLOCK_

	RID id = storage_buffer_owner.make_rid(buffer);
#ifdef DEV_ENABLED
	set_resource_name(id, "RID:" + itos(id.get_id()));
#endif
	return id;
}

RID RenderingDevice::texture_buffer_create(uint32_t p_size_elements, DataFormat p_format, const Vector<uint8_t> &p_data) {
	uint32_t element_size = get_format_vertex_size(p_format);
	ERR_FAIL_COND_V_MSG(element_size == 0, RID(), "Format requested is not supported for texture buffers");
	uint64_t size_bytes = uint64_t(element_size) * p_size_elements;

	ERR_FAIL_COND_V(p_data.size() && (uint32_t)p_data.size() != size_bytes, RID());

	Buffer texture_buffer;
	texture_buffer.size = size_bytes;
	BitField<RDD::BufferUsageBits> usage = (RDD::BUFFER_USAGE_TRANSFER_FROM_BIT | RDD::BUFFER_USAGE_TRANSFER_TO_BIT | RDD::BUFFER_USAGE_TEXEL_BIT);
	texture_buffer.driver_id = driver->buffer_create(size_bytes, usage, RDD::MEMORY_ALLOCATION_TYPE_GPU);
	ERR_FAIL_COND_V(!texture_buffer.driver_id, RID());

	// Texture buffers are assumed to be immutable unless they don't have initial data.
	if (p_data.is_empty()) {
		texture_buffer.draw_tracker = RDG::resource_tracker_create();
		texture_buffer.draw_tracker->buffer_driver_id = texture_buffer.driver_id;
	}

	bool ok = driver->buffer_set_texel_format(texture_buffer.driver_id, p_format);
	if (!ok) {
		driver->buffer_free(texture_buffer.driver_id);
		ERR_FAIL_V(RID());
	}

	if (p_data.size()) {
		_buffer_initialize(&texture_buffer, p_data.ptr(), p_data.size());
	}

	_THREAD_SAFE_LOCK_
	buffer_memory += size_bytes;
	_THREAD_SAFE_UNLOCK_

	RID id = texture_buffer_owner.make_rid(texture_buffer);
#ifdef DEV_ENABLED
	set_resource_name(id, "RID:" + itos(id.get_id()));
#endif
	return id;
}

/*****************/
/**** TEXTURE ****/
/*****************/

RID RenderingDevice::texture_create(const TextureFormat &p_format, const TextureView &p_view, const Vector<Vector<uint8_t>> &p_data) {
	// Some adjustments will happen.
	TextureFormat format = p_format;

	if (format.shareable_formats.size()) {
		ERR_FAIL_COND_V_MSG(!format.shareable_formats.has(format.format), RID(),
				"If supplied a list of shareable formats, the current format must be present in the list");
		ERR_FAIL_COND_V_MSG(p_view.format_override != DATA_FORMAT_MAX && !format.shareable_formats.has(p_view.format_override), RID(),
				"If supplied a list of shareable formats, the current view format override must be present in the list");
	}

	ERR_FAIL_INDEX_V(format.texture_type, RDD::TEXTURE_TYPE_MAX, RID());

	ERR_FAIL_COND_V_MSG(format.width < 1, RID(), "Width must be equal or greater than 1 for all textures");

	if (format.texture_type != TEXTURE_TYPE_1D && format.texture_type != TEXTURE_TYPE_1D_ARRAY) {
		ERR_FAIL_COND_V_MSG(format.height < 1, RID(), "Height must be equal or greater than 1 for 2D and 3D textures");
	}

	if (format.texture_type == TEXTURE_TYPE_3D) {
		ERR_FAIL_COND_V_MSG(format.depth < 1, RID(), "Depth must be equal or greater than 1 for 3D textures");
	}

	ERR_FAIL_COND_V(format.mipmaps < 1, RID());

	if (format.texture_type == TEXTURE_TYPE_1D_ARRAY || format.texture_type == TEXTURE_TYPE_2D_ARRAY || format.texture_type == TEXTURE_TYPE_CUBE_ARRAY || format.texture_type == TEXTURE_TYPE_CUBE) {
		ERR_FAIL_COND_V_MSG(format.array_layers < 1, RID(),
				"Number of layers must be equal or greater than 1 for arrays and cubemaps.");
		ERR_FAIL_COND_V_MSG((format.texture_type == TEXTURE_TYPE_CUBE_ARRAY || format.texture_type == TEXTURE_TYPE_CUBE) && (format.array_layers % 6) != 0, RID(),
				"Cubemap and cubemap array textures must provide a layer number that is multiple of 6");
		ERR_FAIL_COND_V_MSG(format.array_layers > driver->limit_get(LIMIT_MAX_TEXTURE_ARRAY_LAYERS), RID(), "Number of layers exceeds device maximum.");
	} else {
		format.array_layers = 1;
	}

	ERR_FAIL_INDEX_V(format.samples, TEXTURE_SAMPLES_MAX, RID());

	ERR_FAIL_COND_V_MSG(format.usage_bits == 0, RID(), "No usage bits specified (at least one is needed)");

	format.height = format.texture_type != TEXTURE_TYPE_1D && format.texture_type != TEXTURE_TYPE_1D_ARRAY ? format.height : 1;
	format.depth = format.texture_type == TEXTURE_TYPE_3D ? format.depth : 1;

	uint64_t size_max = 0;
	switch (format.texture_type) {
		case TEXTURE_TYPE_1D:
		case TEXTURE_TYPE_1D_ARRAY:
			size_max = driver->limit_get(LIMIT_MAX_TEXTURE_SIZE_1D);
			break;
		case TEXTURE_TYPE_2D:
		case TEXTURE_TYPE_2D_ARRAY:
			size_max = driver->limit_get(LIMIT_MAX_TEXTURE_SIZE_2D);
			break;
		case TEXTURE_TYPE_CUBE:
		case TEXTURE_TYPE_CUBE_ARRAY:
			size_max = driver->limit_get(LIMIT_MAX_TEXTURE_SIZE_CUBE);
			break;
		case TEXTURE_TYPE_3D:
			size_max = driver->limit_get(LIMIT_MAX_TEXTURE_SIZE_3D);
			break;
		case TEXTURE_TYPE_MAX:
			break;
	}
	ERR_FAIL_COND_V_MSG(format.width > size_max || format.height > size_max || format.depth > size_max, RID(), "Texture dimensions exceed device maximum.");

	uint32_t required_mipmaps = get_image_required_mipmaps(format.width, format.height, format.depth);

	ERR_FAIL_COND_V_MSG(required_mipmaps < format.mipmaps, RID(),
			"Too many mipmaps requested for texture format and dimensions (" + itos(format.mipmaps) + "), maximum allowed: (" + itos(required_mipmaps) + ").");

	uint32_t forced_usage_bits = 0;
	if (p_data.size()) {
		ERR_FAIL_COND_V_MSG(p_data.size() != (int)format.array_layers, RID(),
				"Default supplied data for image format is of invalid length (" + itos(p_data.size()) + "), should be (" + itos(format.array_layers) + ").");

		for (uint32_t i = 0; i < format.array_layers; i++) {
			uint32_t required_size = get_image_format_required_size(format.format, format.width, format.height, format.depth, format.mipmaps);
			ERR_FAIL_COND_V_MSG((uint32_t)p_data[i].size() != required_size, RID(),
					"Data for slice index " + itos(i) + " (mapped to layer " + itos(i) + ") differs in size (supplied: " + itos(p_data[i].size()) + ") than what is required by the format (" + itos(required_size) + ").");
		}

		ERR_FAIL_COND_V_MSG(format.usage_bits & TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, RID(),
				"Textures created as depth attachments can't be initialized with data directly. Use RenderingDevice::texture_update() instead.");

		if (!(format.usage_bits & TEXTURE_USAGE_CAN_UPDATE_BIT)) {
			forced_usage_bits = TEXTURE_USAGE_CAN_UPDATE_BIT;
		}
	}

	{
		// Validate that this image is supported for the intended use.
		bool cpu_readable = (format.usage_bits & RDD::TEXTURE_USAGE_CPU_READ_BIT);
		BitField<RDD::TextureUsageBits> supported_usage = driver->texture_get_usages_supported_by_format(format.format, cpu_readable);

		String format_text = "'" + String(FORMAT_NAMES[format.format]) + "'";

		if ((format.usage_bits & TEXTURE_USAGE_SAMPLING_BIT) && !supported_usage.has_flag(TEXTURE_USAGE_SAMPLING_BIT)) {
			ERR_FAIL_V_MSG(RID(), "Format " + format_text + " does not support usage as sampling texture.");
		}
		if ((format.usage_bits & TEXTURE_USAGE_COLOR_ATTACHMENT_BIT) && !supported_usage.has_flag(TEXTURE_USAGE_COLOR_ATTACHMENT_BIT)) {
			ERR_FAIL_V_MSG(RID(), "Format " + format_text + " does not support usage as color attachment.");
		}
		if ((format.usage_bits & TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) && !supported_usage.has_flag(TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
			ERR_FAIL_V_MSG(RID(), "Format " + format_text + " does not support usage as depth-stencil attachment.");
		}
		if ((format.usage_bits & TEXTURE_USAGE_STORAGE_BIT) && !supported_usage.has_flag(TEXTURE_USAGE_STORAGE_BIT)) {
			ERR_FAIL_V_MSG(RID(), "Format " + format_text + " does not support usage as storage image.");
		}
		if ((format.usage_bits & TEXTURE_USAGE_STORAGE_ATOMIC_BIT) && !supported_usage.has_flag(TEXTURE_USAGE_STORAGE_ATOMIC_BIT)) {
			ERR_FAIL_V_MSG(RID(), "Format " + format_text + " does not support usage as atomic storage image.");
		}
		if ((format.usage_bits & TEXTURE_USAGE_VRS_ATTACHMENT_BIT) && !supported_usage.has_flag(TEXTURE_USAGE_VRS_ATTACHMENT_BIT)) {
			ERR_FAIL_V_MSG(RID(), "Format " + format_text + " does not support usage as VRS attachment.");
		}
	}

	// Transfer and validate view info.

	// 驱动侧的纹理视图
	RDD::TextureView tv;
	if (p_view.format_override == DATA_FORMAT_MAX) {
		tv.format = format.format;
	} else {
		ERR_FAIL_INDEX_V(p_view.format_override, DATA_FORMAT_MAX, RID());
		tv.format = p_view.format_override;
	}
	ERR_FAIL_INDEX_V(p_view.swizzle_r, TEXTURE_SWIZZLE_MAX, RID());
	ERR_FAIL_INDEX_V(p_view.swizzle_g, TEXTURE_SWIZZLE_MAX, RID());
	ERR_FAIL_INDEX_V(p_view.swizzle_b, TEXTURE_SWIZZLE_MAX, RID());
	ERR_FAIL_INDEX_V(p_view.swizzle_a, TEXTURE_SWIZZLE_MAX, RID());
	tv.swizzle_r = p_view.swizzle_r;
	tv.swizzle_g = p_view.swizzle_g;
	tv.swizzle_b = p_view.swizzle_b;
	tv.swizzle_a = p_view.swizzle_a;

	// Create.

	Texture texture;
	format.usage_bits |= forced_usage_bits;
	texture.driver_id = driver->texture_create(format, tv);		// 创建纹理，驱动侧的纹理ID返回
	ERR_FAIL_COND_V(!texture.driver_id, RID());
	texture.type = format.texture_type;
	texture.format = format.format;
	texture.width = format.width;
	texture.height = format.height;
	texture.depth = format.depth;
	texture.layers = format.array_layers;
	texture.mipmaps = format.mipmaps;
	texture.base_mipmap = 0;
	texture.base_layer = 0;
	texture.is_resolve_buffer = format.is_resolve_buffer;
	texture.is_discardable = format.is_discardable;
	texture.usage_flags = format.usage_bits & ~forced_usage_bits;
	texture.samples = format.samples;
	texture.allowed_shared_formats = format.shareable_formats;
	texture.has_initial_data = !p_data.is_empty();

	if ((format.usage_bits & TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
		texture.read_aspect_flags.set_flag(RDD::TEXTURE_ASPECT_DEPTH_BIT);
		texture.barrier_aspect_flags.set_flag(RDD::TEXTURE_ASPECT_DEPTH_BIT);
		if (format_has_stencil(format.format)) {
			texture.barrier_aspect_flags.set_flag(RDD::TEXTURE_ASPECT_STENCIL_BIT);
		}
	} else {
		texture.read_aspect_flags.set_flag(RDD::TEXTURE_ASPECT_COLOR_BIT);
		texture.barrier_aspect_flags.set_flag(RDD::TEXTURE_ASPECT_COLOR_BIT);
	}

	texture.bound = false;

	// Textures are only assumed to be immutable if they have initial data and none of the other bits that indicate write usage are enabled.
	bool texture_mutable_by_default = texture.usage_flags & (TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | TEXTURE_USAGE_STORAGE_BIT | TEXTURE_USAGE_STORAGE_ATOMIC_BIT | TEXTURE_USAGE_VRS_ATTACHMENT_BIT);
	if (p_data.is_empty() || texture_mutable_by_default) {
		_texture_make_mutable(&texture, RID());
	}

	texture_memory += driver->texture_get_allocation_size(texture.driver_id);

	RID id = texture_owner.make_rid(texture);	// 将纹理结构转换成RID
#ifdef DEV_ENABLED
	set_resource_name(id, "RID:" + itos(id.get_id()));
#endif

	if (p_data.size()) {
		for (uint32_t i = 0; i < p_format.array_layers; i++) {
			_texture_initialize(id, i, p_data[i]);
		}

		if (texture.draw_tracker != nullptr) {
			// Draw tracker can assume the texture will be in copy destination.
			// 假定其用途是copy to
			texture.draw_tracker->usage = RDG::RESOURCE_USAGE_COPY_TO;
		}
	}

	return id;
}

RID RenderingDevice::texture_create_shared(const TextureView &p_view, RID p_with_texture) {
	Texture *src_texture = texture_owner.get_or_null(p_with_texture);
	ERR_FAIL_NULL_V(src_texture, RID());

	if (src_texture->owner.is_valid()) { // Ahh this is a share. The RenderingDeviceDriver needs the actual owner.
		p_with_texture = src_texture->owner;
		src_texture = texture_owner.get_or_null(src_texture->owner);
		ERR_FAIL_NULL_V(src_texture, RID()); // This is a bug.
	}

	// Create view.

	Texture texture = *src_texture;
	texture.shared_fallback = nullptr;

	RDD::TextureView tv;
	bool create_shared = true;
	bool raw_reintepretation = false;
	if (p_view.format_override == DATA_FORMAT_MAX || p_view.format_override == texture.format) {
		tv.format = texture.format;
	} else {
		ERR_FAIL_INDEX_V(p_view.format_override, DATA_FORMAT_MAX, RID());

		ERR_FAIL_COND_V_MSG(!texture.allowed_shared_formats.has(p_view.format_override), RID(),
				"Format override is not in the list of allowed shareable formats for original texture.");
		tv.format = p_view.format_override;
		create_shared = driver->texture_can_make_shared_with_format(texture.driver_id, p_view.format_override, raw_reintepretation);
	}
	tv.swizzle_r = p_view.swizzle_r;
	tv.swizzle_g = p_view.swizzle_g;
	tv.swizzle_b = p_view.swizzle_b;
	tv.swizzle_a = p_view.swizzle_a;

	if (create_shared) {
		texture.driver_id = driver->texture_create_shared(texture.driver_id, tv);
	} else {
		// The regular view will use the same format as the main texture.
		RDD::TextureView regular_view = tv;
		regular_view.format = src_texture->format;
		texture.driver_id = driver->texture_create_shared(texture.driver_id, regular_view);

		// Create the independent texture for the alias.
		RDD::TextureFormat alias_format = texture.texture_format();
		alias_format.format = tv.format;
		alias_format.usage_bits = TEXTURE_USAGE_SAMPLING_BIT | TEXTURE_USAGE_CAN_COPY_TO_BIT;

		_texture_check_shared_fallback(src_texture);
		_texture_check_shared_fallback(&texture);

		texture.shared_fallback->texture = driver->texture_create(alias_format, tv);
		texture.shared_fallback->raw_reinterpretation = raw_reintepretation;
		texture_memory += driver->texture_get_allocation_size(texture.shared_fallback->texture);

		RDG::ResourceTracker *tracker = RDG::resource_tracker_create();
		tracker->texture_driver_id = texture.shared_fallback->texture;
		tracker->texture_size = Size2i(texture.width, texture.height);
		tracker->texture_subresources = texture.barrier_range();
		tracker->texture_usage = alias_format.usage_bits;
		tracker->is_discardable = texture.is_discardable;
		tracker->reference_count = 1;
		texture.shared_fallback->texture_tracker = tracker;
		texture.shared_fallback->revision = 0;

		if (raw_reintepretation && src_texture->shared_fallback->buffer.id == 0) {
			// For shared textures of the same size, we create the buffer on the main texture if it doesn't have it already.
			_texture_create_reinterpret_buffer(src_texture);
		}
	}

	ERR_FAIL_COND_V(!texture.driver_id, RID());

	texture.slice_trackers.clear();

	if (texture.draw_tracker != nullptr) {
		texture.draw_tracker->reference_count++;
	}

	texture.owner = p_with_texture;
	RID id = texture_owner.make_rid(texture);
#ifdef DEV_ENABLED
	set_resource_name(id, "RID:" + itos(id.get_id()));
#endif
	_add_dependency(id, p_with_texture);

	return id;
}

RID RenderingDevice::texture_create_from_extension(TextureType p_type, DataFormat p_format, TextureSamples p_samples, BitField<RenderingDevice::TextureUsageBits> p_usage, uint64_t p_image, uint64_t p_width, uint64_t p_height, uint64_t p_depth, uint64_t p_layers) {
	// This method creates a texture object using a VkImage created by an extension, module or other external source (OpenXR uses this).

	Texture texture;
	texture.type = p_type;
	texture.format = p_format;
	texture.samples = p_samples;
	texture.width = p_width;
	texture.height = p_height;
	texture.depth = p_depth;
	texture.layers = p_layers;
	texture.mipmaps = 1;
	texture.usage_flags = p_usage;
	texture.base_mipmap = 0;
	texture.base_layer = 0;
	texture.allowed_shared_formats.push_back(RD::DATA_FORMAT_R8G8B8A8_UNORM);
	texture.allowed_shared_formats.push_back(RD::DATA_FORMAT_R8G8B8A8_SRGB);

	if (p_usage.has_flag(TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
		texture.read_aspect_flags.set_flag(RDD::TEXTURE_ASPECT_DEPTH_BIT);
		texture.barrier_aspect_flags.set_flag(RDD::TEXTURE_ASPECT_DEPTH_BIT);
		/*if (format_has_stencil(p_format.format)) {
			texture.barrier_aspect_flags.set_flag(RDD::TEXTURE_ASPECT_STENCIL_BIT);
		}*/
	} else {
		texture.read_aspect_flags.set_flag(RDD::TEXTURE_ASPECT_COLOR_BIT);
		texture.barrier_aspect_flags.set_flag(RDD::TEXTURE_ASPECT_COLOR_BIT);
	}

	texture.driver_id = driver->texture_create_from_extension(p_image, p_type, p_format, p_layers, (texture.usage_flags & TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT));
	ERR_FAIL_COND_V(!texture.driver_id, RID());

	_texture_make_mutable(&texture, RID());

	RID id = texture_owner.make_rid(texture);
#ifdef DEV_ENABLED
	set_resource_name(id, "RID:" + itos(id.get_id()));
#endif

	return id;
}

RID RenderingDevice::texture_create_shared_from_slice(const TextureView &p_view, RID p_with_texture, uint32_t p_layer, uint32_t p_mipmap, uint32_t p_mipmaps, TextureSliceType p_slice_type, uint32_t p_layers) {
	Texture *src_texture = texture_owner.get_or_null(p_with_texture);
	ERR_FAIL_NULL_V(src_texture, RID());

	if (src_texture->owner.is_valid()) { // // Ahh this is a share. The RenderingDeviceDriver needs the actual owner.
		p_with_texture = src_texture->owner;
		src_texture = texture_owner.get_or_null(src_texture->owner);
		ERR_FAIL_NULL_V(src_texture, RID()); // This is a bug.
	}

	ERR_FAIL_COND_V_MSG(p_slice_type == TEXTURE_SLICE_CUBEMAP && (src_texture->type != TEXTURE_TYPE_CUBE && src_texture->type != TEXTURE_TYPE_CUBE_ARRAY), RID(),
			"Can only create a cubemap slice from a cubemap or cubemap array mipmap");

	ERR_FAIL_COND_V_MSG(p_slice_type == TEXTURE_SLICE_3D && src_texture->type != TEXTURE_TYPE_3D, RID(),
			"Can only create a 3D slice from a 3D texture");

	ERR_FAIL_COND_V_MSG(p_slice_type == TEXTURE_SLICE_2D_ARRAY && (src_texture->type != TEXTURE_TYPE_2D_ARRAY), RID(),
			"Can only create an array slice from a 2D array mipmap");

	// Create view.

	ERR_FAIL_UNSIGNED_INDEX_V(p_mipmap, src_texture->mipmaps, RID());
	ERR_FAIL_COND_V(p_mipmap + p_mipmaps > src_texture->mipmaps, RID());
	ERR_FAIL_UNSIGNED_INDEX_V(p_layer, src_texture->layers, RID());

	int slice_layers = 1;
	if (p_layers != 0) {
		ERR_FAIL_COND_V_MSG(p_layers > 1 && p_slice_type != TEXTURE_SLICE_2D_ARRAY, RID(), "layer slicing only supported for 2D arrays");
		ERR_FAIL_COND_V_MSG(p_layer + p_layers > src_texture->layers, RID(), "layer slice is out of bounds");
		slice_layers = p_layers;
	} else if (p_slice_type == TEXTURE_SLICE_2D_ARRAY) {
		ERR_FAIL_COND_V_MSG(p_layer != 0, RID(), "layer must be 0 when obtaining a 2D array mipmap slice");
		slice_layers = src_texture->layers;
	} else if (p_slice_type == TEXTURE_SLICE_CUBEMAP) {
		slice_layers = 6;
	}

	Texture texture = *src_texture;
	texture.shared_fallback = nullptr;

	get_image_format_required_size(texture.format, texture.width, texture.height, texture.depth, p_mipmap + 1, &texture.width, &texture.height);
	texture.mipmaps = p_mipmaps;
	texture.layers = slice_layers;
	texture.base_mipmap = p_mipmap;
	texture.base_layer = p_layer;

	if (p_slice_type == TEXTURE_SLICE_2D) {
		texture.type = TEXTURE_TYPE_2D;
	} else if (p_slice_type == TEXTURE_SLICE_3D) {
		texture.type = TEXTURE_TYPE_3D;
	}

	RDD::TextureView tv;
	bool create_shared = true;
	bool raw_reintepretation = false;
	if (p_view.format_override == DATA_FORMAT_MAX || p_view.format_override == texture.format) {
		tv.format = texture.format;
	} else {
		ERR_FAIL_INDEX_V(p_view.format_override, DATA_FORMAT_MAX, RID());

		ERR_FAIL_COND_V_MSG(!texture.allowed_shared_formats.has(p_view.format_override), RID(),
				"Format override is not in the list of allowed shareable formats for original texture.");
		tv.format = p_view.format_override;
		create_shared = driver->texture_can_make_shared_with_format(texture.driver_id, p_view.format_override, raw_reintepretation);
	}

	tv.swizzle_r = p_view.swizzle_r;
	tv.swizzle_g = p_view.swizzle_g;
	tv.swizzle_b = p_view.swizzle_b;
	tv.swizzle_a = p_view.swizzle_a;

	if (p_slice_type == TEXTURE_SLICE_CUBEMAP) {
		ERR_FAIL_COND_V_MSG(p_layer >= src_texture->layers, RID(),
				"Specified layer is invalid for cubemap");
		ERR_FAIL_COND_V_MSG((p_layer % 6) != 0, RID(),
				"Specified layer must be a multiple of 6.");
	}

	if (create_shared) {
		texture.driver_id = driver->texture_create_shared_from_slice(src_texture->driver_id, tv, p_slice_type, p_layer, slice_layers, p_mipmap, p_mipmaps);
	} else {
		// The regular view will use the same format as the main texture.
		RDD::TextureView regular_view = tv;
		regular_view.format = src_texture->format;
		texture.driver_id = driver->texture_create_shared_from_slice(src_texture->driver_id, regular_view, p_slice_type, p_layer, slice_layers, p_mipmap, p_mipmaps);

		// Create the independent texture for the slice.
		RDD::TextureSubresourceRange slice_range = texture.barrier_range();
		slice_range.base_mipmap = 0;
		slice_range.base_layer = 0;

		RDD::TextureFormat slice_format = texture.texture_format();
		slice_format.width = MAX(texture.width >> p_mipmap, 1U);
		slice_format.height = MAX(texture.height >> p_mipmap, 1U);
		slice_format.depth = MAX(texture.depth >> p_mipmap, 1U);
		slice_format.format = tv.format;
		slice_format.usage_bits = TEXTURE_USAGE_SAMPLING_BIT | TEXTURE_USAGE_CAN_COPY_TO_BIT;

		_texture_check_shared_fallback(src_texture);
		_texture_check_shared_fallback(&texture);

		texture.shared_fallback->texture = driver->texture_create(slice_format, tv);
		texture.shared_fallback->raw_reinterpretation = raw_reintepretation;
		texture_memory += driver->texture_get_allocation_size(texture.shared_fallback->texture);

		RDG::ResourceTracker *tracker = RDG::resource_tracker_create();
		tracker->texture_driver_id = texture.shared_fallback->texture;
		tracker->texture_size = Size2i(texture.width, texture.height);
		tracker->texture_subresources = slice_range;
		tracker->texture_usage = slice_format.usage_bits;
		tracker->is_discardable = slice_format.is_discardable;
		tracker->reference_count = 1;
		texture.shared_fallback->texture_tracker = tracker;
		texture.shared_fallback->revision = 0;

		if (raw_reintepretation && src_texture->shared_fallback->buffer.id == 0) {
			// For shared texture slices, we create the buffer on the slice if the source texture has no reinterpretation buffer.
			_texture_create_reinterpret_buffer(&texture);
		}
	}

	ERR_FAIL_COND_V(!texture.driver_id, RID());

	const Rect2i slice_rect(p_mipmap, p_layer, p_mipmaps, slice_layers);
	texture.owner = p_with_texture;
	texture.slice_type = p_slice_type;
	texture.slice_rect = slice_rect;

	// If parent is mutable, make slice mutable by default.
	if (src_texture->draw_tracker != nullptr) {
		texture.draw_tracker = nullptr;
		_texture_make_mutable(&texture, RID());
	}

	RID id = texture_owner.make_rid(texture);
#ifdef DEV_ENABLED
	set_resource_name(id, "RID:" + itos(id.get_id()));
#endif
	_add_dependency(id, p_with_texture);

	return id;
}

static _ALWAYS_INLINE_ void _copy_region(uint8_t const *__restrict p_src, uint8_t *__restrict p_dst, uint32_t p_src_x, uint32_t p_src_y, uint32_t p_src_w, uint32_t p_src_h, uint32_t p_src_full_w, uint32_t p_dst_pitch, uint32_t p_unit_size) {
	uint32_t src_offset = (p_src_y * p_src_full_w + p_src_x) * p_unit_size;
	uint32_t dst_offset = 0;
	for (uint32_t y = p_src_h; y > 0; y--) {
		uint8_t const *__restrict src = p_src + src_offset;
		uint8_t *__restrict dst = p_dst + dst_offset;
		for (uint32_t x = p_src_w * p_unit_size; x > 0; x--) {
			*dst = *src;
			src++;
			dst++;
		}
		src_offset += p_src_full_w * p_unit_size;
		dst_offset += p_dst_pitch;
	}
}

static _ALWAYS_INLINE_ void _copy_region_block_or_regular(const uint8_t *p_read_ptr, uint8_t *p_write_ptr, uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_region_w, uint32_t p_region_h, uint32_t p_block_w, uint32_t p_block_h, uint32_t p_dst_pitch, uint32_t p_pixel_size, uint32_t p_block_size) {
	if (p_block_w != 1 || p_block_h != 1) {
		// Block format.
		uint32_t xb = p_x / p_block_w;
		uint32_t yb = p_y / p_block_h;
		uint32_t wb = p_width / p_block_w;
		uint32_t region_wb = p_region_w / p_block_w;
		uint32_t region_hb = p_region_h / p_block_h;
		_copy_region(p_read_ptr, p_write_ptr, xb, yb, region_wb, region_hb, wb, p_dst_pitch, p_block_size);
	} else {
		// Regular format.
		_copy_region(p_read_ptr, p_write_ptr, p_x, p_y, p_region_w, p_region_h, p_width, p_dst_pitch, p_pixel_size);
	}
}

uint32_t RenderingDevice::_texture_layer_count(Texture *p_texture) const {
	switch (p_texture->type) {
		case TEXTURE_TYPE_CUBE:
		case TEXTURE_TYPE_CUBE_ARRAY:
			return p_texture->layers * 6;
		default:
			return p_texture->layers;
	}
}

uint32_t RenderingDevice::_texture_alignment(Texture *p_texture) const {
	uint32_t alignment = get_compressed_image_format_block_byte_size(p_texture->format);
	if (alignment == 1) {
		alignment = get_image_format_pixel_size(p_texture->format);
	}

	return STEPIFY(alignment, driver->api_trait_get(RDD::API_TRAIT_TEXTURE_TRANSFER_ALIGNMENT));
}

/**
 *
 * 把你传进来的 p_data（CPU 上的一整块纹理字节数据），按 mip、按层、按对齐规则，拷到一个 staging buffer，
 * 再从 staging buffer 发命令复制进 GPU 纹理（某一层），顺便做 layout 转换和 barrier。
 */
Error RenderingDevice::_texture_initialize(RID p_texture, uint32_t p_layer, const Vector<uint8_t> &p_data) {
	// 这是godot对应的texture概念
	Texture *texture = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(texture, ERR_INVALID_PARAMETER);

	// 如果这个 Texture 本身是“别的纹理的 owner 的引用”（比如 alias / view），就跳到真正的 owner 上。
	if (texture->owner != RID()) {
		p_texture = texture->owner;
		texture = texture_owner.get_or_null(texture->owner);	// owner能否有别的owner？
		ERR_FAIL_NULL_V(texture, ERR_BUG); // This is a bug.
	}

	// 计算这个纹理实际有多少 layer（普通 2D 就是 1，array / 3D 的会大一些）
	uint32_t layer_count = _texture_layer_count(texture);
	ERR_FAIL_COND_V(p_layer >= layer_count, ERR_INVALID_PARAMETER);

	// 根据纹理的 format / width / height / depth / mipmaps，算出 所有 mip 的总字节数（tight_mip_size），以及对齐后真实的 width/height（一般是压缩格式才会对齐）。
	uint32_t width, height;
	uint32_t tight_mip_size = get_image_format_required_size(texture->format, texture->width, texture->height, texture->depth, texture->mipmaps, &width, &height);
	uint32_t required_size = tight_mip_size;
	uint32_t required_align = _texture_alignment(texture);		// 上传到 GPU staging buffer 时需要的对齐粒度（比如 256 bytes 对齐之类）

	ERR_FAIL_COND_V_MSG(required_size != (uint32_t)p_data.size(), ERR_INVALID_PARAMETER,
			"Required size for texture update (" + itos(required_size) + ") does not match data supplied size (" + itos(p_data.size()) + ").");

	// 对压缩格式：拿到一个 block 有多大，比如 BC/ASTC 的 4×4 之类
	// 对非压缩格式，block 一般是 1×1，但函数也统一走这里。
	uint32_t block_w, block_h;
	get_compressed_image_format_block_dimensions(texture->format, block_w, block_h);

	uint32_t pixel_size = get_image_format_pixel_size(texture->format);		// 每个像素单元的字节大小
	uint32_t pixel_rshift = get_compressed_image_format_pixel_rshift(texture->format);	// 针对压缩格式的一些换算
	uint32_t block_size = get_compressed_image_format_block_byte_size(texture->format);		// 一个压缩块占多少字节

	// The algorithm operates on two passes, one to figure out the total size the staging buffer will require to allocate and another one where the copy is actually performed.
	// 这个算法以双pass实现，一个pass算出staging缓冲需要的总大小，另一个获取能容纳这些数据的staging 缓冲，并按计算好的布局拷贝数据，发送copy指令
	uint32_t staging_worker_offset = 0;		// 这个worker的staging buffer全局offset基准
	uint32_t staging_local_offset = 0;		// 当前pass中在这个staging buffer内部的偏移（会加上对齐）
	TransferWorker *transfer_worker = nullptr;		// 内部封装的传输工作
	const uint8_t *read_ptr = p_data.ptr();		// 指向传入的CPU数据
	uint8_t *write_ptr = nullptr;	// 指向staging buffer的CPU映射地址

	// 根据驱动特性，决定复制时目标纹理的layout用什么，优先使用general，不行就用copy dst
	const RDD::TextureLayout copy_dst_layout = driver->api_trait_get(RDD::API_TRAIT_USE_GENERAL_IN_COPY_QUEUES) ? RDD::TEXTURE_LAYOUT_GENERAL : RDD::TEXTURE_LAYOUT_COPY_DST_OPTIMAL;
	for (uint32_t pass = 0; pass < 2; pass++) {
		const bool copy_pass = (pass == 1);
		if (copy_pass) {
			// 拿到一个真正的transfer
			// 函数里会根据第一遍算出的 staging_local_offset 和对齐、再加上其它 worker 状态，给你分配一个足够大的 staging buffer 段，并决定 staging_worker_offset
			transfer_worker = _acquire_transfer_worker(staging_local_offset, required_align, staging_worker_offset);
			texture->transfer_worker_index = transfer_worker->index;	// 记录这个纹理对象正在执行传输操作

			{
				// 线程安全的操作计数器自增，加锁时因为多个线程可能在用同一个worker
				MutexLock lock(transfer_worker->operations_mutex);
				texture->transfer_worker_operation = ++transfer_worker->operations_counter;
			}

			// 第二遍正式开始之前，把“局部 offset”重置为 0（因为第一遍我们只是累计大小，现在再从头开始按同样逻辑重走一遍）
			staging_local_offset = 0;

			// 把这个worker里的staging buffer映射到CPU能访问的内存
			write_ptr = driver->buffer_map(transfer_worker->staging_buffer);
			ERR_FAIL_NULL_V(write_ptr, ERR_CANT_CREATE);

			// 如果底层driver支持/遵守pipeline barrier
			if (driver->api_trait_get(RDD::API_TRAIT_HONORS_PIPELINE_BARRIERS)) {
				// Transition the texture to the optimal layout.
				// 将layout转换成高效的版本，或者说优化版本
				RDD::TextureBarrier tb;
				tb.texture = texture->driver_id;
				tb.dst_access = RDD::BARRIER_ACCESS_COPY_WRITE_BIT;	// 布局从undified到copy dst layout
				tb.prev_layout = RDD::TEXTURE_LAYOUT_UNDEFINED;
				tb.next_layout = copy_dst_layout;
				tb.subresources.aspect = texture->barrier_aspect_flags;
				tb.subresources.mipmap_count = texture->mipmaps;
				tb.subresources.base_layer = p_layer;
				tb.subresources.layer_count = 1;
				driver->command_pipeline_barrier(transfer_worker->command_buffer, RDD::PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, RDD::PIPELINE_STAGE_COPY_BIT, {}, {}, tb);
			}

			// 到这里为止，第二遍时，纹理对应的 GPU image 已经处在“可写”的 layout 上了，我们也有一个可写入的 staging buffer 映射。
		}

		uint32_t mipmap_offset = 0;	// 在p_data里当前mip开始的偏移
		uint32_t logic_width = texture->width;	// mip的逻辑尺寸
		uint32_t logic_height = texture->height;
		// 遍历每个mipmap
		for (uint32_t mm_i = 0; mm_i < texture->mipmaps; mm_i++) {
			uint32_t depth = 0;
			// 获取前mm_i + 1个mip的字节总量
			uint32_t image_total = get_image_format_required_size(texture->format, texture->width, texture->height, texture->depth, mm_i + 1, &width, &height, &depth);

			// 指向第 mm_i 级 mip 在 p_data 中的起始地址
			const uint8_t *read_ptr_mipmap = read_ptr + mipmap_offset;
			// 当前 mip 这一级的字节长度 = 总到此为止 - 上一级的累计偏移。
			tight_mip_size = image_total - mipmap_offset;

			// 针对3D纹理/array的情况，把这个mip切成depth layers（对2D一般depth = 1）
			for (uint32_t z = 0; z < depth; z++) {
				// 确保当前这块数据在staging buffer内部满足required align对齐
				// 不对齐就偏移一些补齐
				if (required_align > 0) {
					uint32_t align_offset = staging_local_offset % required_align;
					if (align_offset != 0) {
						staging_local_offset += required_align - align_offset;
					}
				}

				// 计算一行的字节跨度，在根据偏移调整。
				uint32_t pitch = (width * pixel_size * block_w) >> pixel_rshift;
				uint32_t pitch_step = driver->api_trait_get(RDD::API_TRAIT_TEXTURE_DATA_ROW_PITCH_STEP);	// 底层要求的最小步长
				pitch = STEPIFY(pitch, pitch_step);	// 对齐到步长
				uint32_t to_allocate = pitch * height;	// 这一层的总字节数
				to_allocate >>= pixel_rshift;	// 然后再做压缩换算

				if (copy_pass) {
					// 算出此 z layer 在当前 mip 数据里的 offset
					const uint8_t *read_ptr_mipmap_layer = read_ptr_mipmap + (tight_mip_size / depth) * z;
					// worker的基准offset，+当前局部offset
					uint64_t staging_buffer_offset = staging_worker_offset + staging_local_offset;
					// 通过偏移得到相应的指针
					uint8_t *write_ptr_mipmap_layer = write_ptr + staging_buffer_offset;
					// 根据规则将数据拷贝进去
					_copy_region_block_or_regular(read_ptr_mipmap_layer, write_ptr_mipmap_layer, 0, 0, width, width, height, block_w, block_h, pitch, pixel_size, block_size);

					// 填充一个纹理拷贝区域的结构
					RDD::BufferTextureCopyRegion copy_region;
					copy_region.buffer_offset = staging_buffer_offset;	// 在staging buffer里这块区域的起始点
					copy_region.texture_subresources.aspect = texture->read_aspect_flags;	//
					copy_region.texture_subresources.mipmap = mm_i;
					copy_region.texture_subresources.base_layer = p_layer;
					copy_region.texture_subresources.layer_count = 1;
					copy_region.texture_offset = Vector3i(0, 0, z);	// 纹理到当前层
					copy_region.texture_region_size = Vector3i(logic_width, logic_height, 1);	// 区域的尺寸大小
					// 将拷贝命令录制进去
					driver->command_copy_buffer_to_texture(transfer_worker->command_buffer, transfer_worker->staging_buffer, texture->driver_id, copy_dst_layout, copy_region);
				}

				staging_local_offset += to_allocate;	// 然后是偏移区域
			}

			mipmap_offset = image_total;	// 更新mipmap作为下一个计算起点
			logic_width = MAX(1u, logic_width >> 1);
			logic_height = MAX(1u, logic_height >> 1);
		}

		if (copy_pass) {
			// 完成copy之后就先unmap
			driver->buffer_unmap(transfer_worker->staging_buffer);

			// If the texture does not have a tracker, it means it must be transitioned to the sampling state.
			// 如果没有追踪器，说明它需要立刻被转换成采样状态，但是如果没有pipeline barrier的话也就不做了。
			if (texture->draw_tracker == nullptr && driver->api_trait_get(RDD::API_TRAIT_HONORS_PIPELINE_BARRIERS)) {
				RDD::TextureBarrier tb;
				tb.texture = texture->driver_id;
				tb.src_access = RDD::BARRIER_ACCESS_COPY_WRITE_BIT;
				tb.prev_layout = copy_dst_layout;
				tb.next_layout = RDD::TEXTURE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				tb.subresources.aspect = texture->barrier_aspect_flags;
				tb.subresources.mipmap_count = texture->mipmaps;
				tb.subresources.base_layer = p_layer;
				tb.subresources.layer_count = 1;
				transfer_worker->texture_barriers.push_back(tb);
			}

			// 释放这个worker
			_release_transfer_worker(transfer_worker);
		}
	}

	return OK;
}

Error RenderingDevice::texture_update(RID p_texture, uint32_t p_layer, const Vector<uint8_t> &p_data) {
	ERR_RENDER_THREAD_GUARD_V(ERR_UNAVAILABLE);

	ERR_FAIL_COND_V_MSG(draw_list || compute_list, ERR_INVALID_PARAMETER, "Updating textures is forbidden during creation of a draw or compute list");

	Texture *texture = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(texture, ERR_INVALID_PARAMETER);

	if (texture->owner != RID()) {
		p_texture = texture->owner;
		texture = texture_owner.get_or_null(texture->owner);
		ERR_FAIL_NULL_V(texture, ERR_BUG); // This is a bug.
	}

	ERR_FAIL_COND_V_MSG(texture->bound, ERR_CANT_ACQUIRE_RESOURCE,
			"Texture can't be updated while a draw list that uses it as part of a framebuffer is being created. Ensure the draw list is finalized (and that the color/depth texture using it is not set to `RenderingDevice.FINAL_ACTION_CONTINUE`) to update this texture.");

	ERR_FAIL_COND_V_MSG(!(texture->usage_flags & TEXTURE_USAGE_CAN_UPDATE_BIT), ERR_INVALID_PARAMETER, "Texture requires the `RenderingDevice.TEXTURE_USAGE_CAN_UPDATE_BIT` to be set to be updatable.");

	uint32_t layer_count = _texture_layer_count(texture);
	ERR_FAIL_COND_V(p_layer >= layer_count, ERR_INVALID_PARAMETER);

	uint32_t width, height;
	uint32_t tight_mip_size = get_image_format_required_size(texture->format, texture->width, texture->height, texture->depth, texture->mipmaps, &width, &height);
	uint32_t required_size = tight_mip_size;
	uint32_t required_align = _texture_alignment(texture);

	ERR_FAIL_COND_V_MSG(required_size != (uint32_t)p_data.size(), ERR_INVALID_PARAMETER,
			"Required size for texture update (" + itos(required_size) + ") does not match data supplied size (" + itos(p_data.size()) + ").");

	_check_transfer_worker_texture(texture);

	uint32_t block_w, block_h;
	get_compressed_image_format_block_dimensions(texture->format, block_w, block_h);

	uint32_t pixel_size = get_image_format_pixel_size(texture->format);
	uint32_t pixel_rshift = get_compressed_image_format_pixel_rshift(texture->format);
	uint32_t block_size = get_compressed_image_format_block_byte_size(texture->format);

	uint32_t region_size = texture_upload_region_size_px;

	const uint8_t *read_ptr = p_data.ptr();

	thread_local LocalVector<RDG::RecordedBufferToTextureCopy> command_buffer_to_texture_copies_vector;
	command_buffer_to_texture_copies_vector.clear();

	// Indicate the texture will get modified for the shared texture fallback.
	_texture_update_shared_fallback(p_texture, texture, true);

	uint32_t mipmap_offset = 0;

	uint32_t logic_width = texture->width;
	uint32_t logic_height = texture->height;

	for (uint32_t mm_i = 0; mm_i < texture->mipmaps; mm_i++) {
		uint32_t depth = 0;
		uint32_t image_total = get_image_format_required_size(texture->format, texture->width, texture->height, texture->depth, mm_i + 1, &width, &height, &depth);

		const uint8_t *read_ptr_mipmap = read_ptr + mipmap_offset;
		tight_mip_size = image_total - mipmap_offset;

		for (uint32_t z = 0; z < depth; z++) {
			const uint8_t *read_ptr_mipmap_layer = read_ptr_mipmap + (tight_mip_size / depth) * z;
			for (uint32_t y = 0; y < height; y += region_size) {
				for (uint32_t x = 0; x < width; x += region_size) {
					uint32_t region_w = MIN(region_size, width - x);
					uint32_t region_h = MIN(region_size, height - y);

					uint32_t region_logic_w = MIN(region_size, logic_width - x);
					uint32_t region_logic_h = MIN(region_size, logic_height - y);

					uint32_t region_pitch = (region_w * pixel_size * block_w) >> pixel_rshift;
					uint32_t pitch_step = driver->api_trait_get(RDD::API_TRAIT_TEXTURE_DATA_ROW_PITCH_STEP);
					region_pitch = STEPIFY(region_pitch, pitch_step);
					uint32_t to_allocate = region_pitch * region_h;
					uint32_t alloc_offset = 0, alloc_size = 0;
					StagingRequiredAction required_action;
					Error err = _staging_buffer_allocate(upload_staging_buffers, to_allocate, required_align, alloc_offset, alloc_size, required_action, false);
					ERR_FAIL_COND_V(err, ERR_CANT_CREATE);

					if (!command_buffer_to_texture_copies_vector.is_empty() && required_action == STAGING_REQUIRED_ACTION_FLUSH_AND_STALL_ALL) {
						if (_texture_make_mutable(texture, p_texture)) {
							// The texture must be mutable to be used as a copy destination.
							draw_graph.add_synchronization();
						}

						// If the staging buffer requires flushing everything, we submit the command early and clear the current vector.
						draw_graph.add_texture_update(texture->driver_id, texture->draw_tracker, command_buffer_to_texture_copies_vector);
						command_buffer_to_texture_copies_vector.clear();
					}

					_staging_buffer_execute_required_action(upload_staging_buffers, required_action);

					uint8_t *write_ptr;

					{ // Map.
						uint8_t *data_ptr = driver->buffer_map(upload_staging_buffers.blocks[upload_staging_buffers.current].driver_id);
						ERR_FAIL_NULL_V(data_ptr, ERR_CANT_CREATE);
						write_ptr = data_ptr;
						write_ptr += alloc_offset;
					}

					ERR_FAIL_COND_V(region_w % block_w, ERR_BUG);
					ERR_FAIL_COND_V(region_h % block_h, ERR_BUG);

					_copy_region_block_or_regular(read_ptr_mipmap_layer, write_ptr, x, y, width, region_w, region_h, block_w, block_h, region_pitch, pixel_size, block_size);

					{ // Unmap.
						driver->buffer_unmap(upload_staging_buffers.blocks[upload_staging_buffers.current].driver_id);
					}

					RDD::BufferTextureCopyRegion copy_region;
					copy_region.buffer_offset = alloc_offset;
					copy_region.texture_subresources.aspect = texture->read_aspect_flags;
					copy_region.texture_subresources.mipmap = mm_i;
					copy_region.texture_subresources.base_layer = p_layer;
					copy_region.texture_subresources.layer_count = 1;
					copy_region.texture_offset = Vector3i(x, y, z);
					copy_region.texture_region_size = Vector3i(region_logic_w, region_logic_h, 1);

					RDG::RecordedBufferToTextureCopy buffer_to_texture_copy;
					buffer_to_texture_copy.from_buffer = upload_staging_buffers.blocks[upload_staging_buffers.current].driver_id;
					buffer_to_texture_copy.region = copy_region;
					command_buffer_to_texture_copies_vector.push_back(buffer_to_texture_copy);

					upload_staging_buffers.blocks.write[upload_staging_buffers.current].fill_amount = alloc_offset + alloc_size;
				}
			}
		}

		mipmap_offset = image_total;
		logic_width = MAX(1u, logic_width >> 1);
		logic_height = MAX(1u, logic_height >> 1);
	}

	if (_texture_make_mutable(texture, p_texture)) {
		// The texture must be mutable to be used as a copy destination.
		draw_graph.add_synchronization();
	}

	draw_graph.add_texture_update(texture->driver_id, texture->draw_tracker, command_buffer_to_texture_copies_vector);

	return OK;
}

void RenderingDevice::_texture_check_shared_fallback(Texture *p_texture) {
	if (p_texture->shared_fallback == nullptr) {
		p_texture->shared_fallback = memnew(Texture::SharedFallback);
	}
}

void RenderingDevice::_texture_update_shared_fallback(RID p_texture_rid, Texture *p_texture, bool p_for_writing) {
	if (p_texture->shared_fallback == nullptr) {
		// This texture does not use any of the shared texture fallbacks.
		return;
	}

	if (p_texture->owner.is_valid()) {
		Texture *owner_texture = texture_owner.get_or_null(p_texture->owner);
		ERR_FAIL_NULL(owner_texture);
		if (p_for_writing) {
			// Only the main texture is used for writing when using the shared fallback.
			owner_texture->shared_fallback->revision++;
		} else if (p_texture->shared_fallback->revision != owner_texture->shared_fallback->revision) {
			// Copy the contents of the main texture into the shared texture fallback slice. Update the revision.
			_texture_copy_shared(p_texture->owner, owner_texture, p_texture_rid, p_texture);
			p_texture->shared_fallback->revision = owner_texture->shared_fallback->revision;
		}
	} else if (p_for_writing) {
		// Increment the revision of the texture so shared texture fallback slices must be updated.
		p_texture->shared_fallback->revision++;
	}
}

void RenderingDevice::_texture_free_shared_fallback(Texture *p_texture) {
	if (p_texture->shared_fallback != nullptr) {
		if (p_texture->shared_fallback->texture_tracker != nullptr) {
			RDG::resource_tracker_free(p_texture->shared_fallback->texture_tracker);
		}

		if (p_texture->shared_fallback->buffer_tracker != nullptr) {
			RDG::resource_tracker_free(p_texture->shared_fallback->buffer_tracker);
		}

		if (p_texture->shared_fallback->texture.id != 0) {
			texture_memory -= driver->texture_get_allocation_size(p_texture->shared_fallback->texture);
			driver->texture_free(p_texture->shared_fallback->texture);
		}

		if (p_texture->shared_fallback->buffer.id != 0) {
			buffer_memory -= driver->buffer_get_allocation_size(p_texture->shared_fallback->buffer);
			driver->buffer_free(p_texture->shared_fallback->buffer);
		}

		memdelete(p_texture->shared_fallback);
		p_texture->shared_fallback = nullptr;
	}
}

void RenderingDevice::_texture_copy_shared(RID p_src_texture_rid, Texture *p_src_texture, RID p_dst_texture_rid, Texture *p_dst_texture) {
	// The only type of copying allowed is from the main texture to the slice texture, as slice textures are not allowed to be used for writing when using this fallback.
	DEV_ASSERT(p_src_texture != nullptr);
	DEV_ASSERT(p_dst_texture != nullptr);
	DEV_ASSERT(p_src_texture->owner.is_null());
	DEV_ASSERT(p_dst_texture->owner == p_src_texture_rid);

	bool src_made_mutable = _texture_make_mutable(p_src_texture, p_src_texture_rid);
	bool dst_made_mutable = _texture_make_mutable(p_dst_texture, p_dst_texture_rid);
	if (src_made_mutable || dst_made_mutable) {
		draw_graph.add_synchronization();
	}

	if (p_dst_texture->shared_fallback->raw_reinterpretation) {
		// If one of the textures is a main texture and they have a reinterpret buffer, we prefer using that as it's guaranteed to be big enough to hold
		// anything and it's how the shared textures that don't use slices are created.
		bool src_has_buffer = p_src_texture->shared_fallback->buffer.id != 0;
		bool dst_has_buffer = p_dst_texture->shared_fallback->buffer.id != 0;
		bool from_src = p_src_texture->owner.is_null() && src_has_buffer;
		bool from_dst = p_dst_texture->owner.is_null() && dst_has_buffer;
		if (!from_src && !from_dst) {
			// If neither texture passed the condition, we just pick whichever texture has a reinterpretation buffer.
			from_src = src_has_buffer;
			from_dst = dst_has_buffer;
		}

		// Pick the buffer and tracker to use from the right texture.
		RDD::BufferID shared_buffer;
		RDG::ResourceTracker *shared_buffer_tracker = nullptr;
		if (from_src) {
			shared_buffer = p_src_texture->shared_fallback->buffer;
			shared_buffer_tracker = p_src_texture->shared_fallback->buffer_tracker;
		} else if (from_dst) {
			shared_buffer = p_dst_texture->shared_fallback->buffer;
			shared_buffer_tracker = p_dst_texture->shared_fallback->buffer_tracker;
		} else {
			DEV_ASSERT(false && "This path should not be reachable.");
		}

		// FIXME: When using reinterpretation buffers, the only texture aspect supported is color. Depth or stencil contents won't get copied.
		RDD::BufferTextureCopyRegion get_data_region;
		RDG::RecordedBufferToTextureCopy update_copy;
		RDD::TextureCopyableLayout first_copyable_layout;
		RDD::TextureCopyableLayout copyable_layout;
		RDD::TextureSubresource texture_subresource;
		texture_subresource.aspect = RDD::TEXTURE_ASPECT_COLOR;
		texture_subresource.layer = 0;
		texture_subresource.mipmap = 0;
		driver->texture_get_copyable_layout(p_dst_texture->shared_fallback->texture, texture_subresource, &first_copyable_layout);

		// Copying each mipmap from main texture to a buffer and then to the slice texture.
		thread_local LocalVector<RDD::BufferTextureCopyRegion> get_data_vector;
		thread_local LocalVector<RDG::RecordedBufferToTextureCopy> update_vector;
		get_data_vector.clear();
		update_vector.clear();
		for (uint32_t i = 0; i < p_dst_texture->mipmaps; i++) {
			driver->texture_get_copyable_layout(p_dst_texture->shared_fallback->texture, texture_subresource, &copyable_layout);

			uint32_t mipmap = p_dst_texture->base_mipmap + i;
			get_data_region.buffer_offset = copyable_layout.offset - first_copyable_layout.offset;
			get_data_region.texture_subresources.aspect = RDD::TEXTURE_ASPECT_COLOR_BIT;
			get_data_region.texture_subresources.base_layer = p_dst_texture->base_layer;
			get_data_region.texture_subresources.mipmap = mipmap;
			get_data_region.texture_subresources.layer_count = p_dst_texture->layers;
			get_data_region.texture_region_size.x = MAX(1U, p_src_texture->width >> mipmap);
			get_data_region.texture_region_size.y = MAX(1U, p_src_texture->height >> mipmap);
			get_data_region.texture_region_size.z = MAX(1U, p_src_texture->depth >> mipmap);
			get_data_vector.push_back(get_data_region);

			update_copy.from_buffer = shared_buffer;
			update_copy.region.buffer_offset = get_data_region.buffer_offset;
			update_copy.region.texture_subresources.aspect = RDD::TEXTURE_ASPECT_COLOR_BIT;
			update_copy.region.texture_subresources.base_layer = texture_subresource.layer;
			update_copy.region.texture_subresources.mipmap = texture_subresource.mipmap;
			update_copy.region.texture_subresources.layer_count = get_data_region.texture_subresources.layer_count;
			update_copy.region.texture_region_size.x = get_data_region.texture_region_size.x;
			update_copy.region.texture_region_size.y = get_data_region.texture_region_size.y;
			update_copy.region.texture_region_size.z = get_data_region.texture_region_size.z;
			update_vector.push_back(update_copy);

			texture_subresource.mipmap++;
		}

		draw_graph.add_texture_get_data(p_src_texture->driver_id, p_src_texture->draw_tracker, shared_buffer, get_data_vector, shared_buffer_tracker);
		draw_graph.add_texture_update(p_dst_texture->shared_fallback->texture, p_dst_texture->shared_fallback->texture_tracker, update_vector, shared_buffer_tracker);
	} else {
		// Raw reinterpretation is not required. Use a regular texture copy.
		RDD::TextureCopyRegion copy_region;
		copy_region.src_subresources.aspect = p_src_texture->read_aspect_flags;
		copy_region.src_subresources.base_layer = p_dst_texture->base_layer;
		copy_region.src_subresources.layer_count = p_dst_texture->layers;
		copy_region.dst_subresources.aspect = p_dst_texture->read_aspect_flags;
		copy_region.dst_subresources.base_layer = 0;
		copy_region.dst_subresources.layer_count = copy_region.src_subresources.layer_count;

		// Copying each mipmap from main texture to to the slice texture.
		thread_local LocalVector<RDD::TextureCopyRegion> region_vector;
		region_vector.clear();
		for (uint32_t i = 0; i < p_dst_texture->mipmaps; i++) {
			uint32_t mipmap = p_dst_texture->base_mipmap + i;
			copy_region.src_subresources.mipmap = mipmap;
			copy_region.dst_subresources.mipmap = i;
			copy_region.size.x = MAX(1U, p_src_texture->width >> mipmap);
			copy_region.size.y = MAX(1U, p_src_texture->height >> mipmap);
			copy_region.size.z = MAX(1U, p_src_texture->depth >> mipmap);
			region_vector.push_back(copy_region);
		}

		draw_graph.add_texture_copy(p_src_texture->driver_id, p_src_texture->draw_tracker, p_dst_texture->shared_fallback->texture, p_dst_texture->shared_fallback->texture_tracker, region_vector);
	}
}

void RenderingDevice::_texture_create_reinterpret_buffer(Texture *p_texture) {
	uint64_t row_pitch_step = driver->api_trait_get(RDD::API_TRAIT_TEXTURE_DATA_ROW_PITCH_STEP);
	uint64_t transfer_alignment = driver->api_trait_get(RDD::API_TRAIT_TEXTURE_TRANSFER_ALIGNMENT);
	uint32_t pixel_bytes = get_image_format_pixel_size(p_texture->format);
	uint32_t row_pitch = STEPIFY(p_texture->width * pixel_bytes, row_pitch_step);
	uint64_t buffer_size = STEPIFY(pixel_bytes * row_pitch * p_texture->height * p_texture->depth, transfer_alignment);
	p_texture->shared_fallback->buffer = driver->buffer_create(buffer_size, RDD::BUFFER_USAGE_TRANSFER_FROM_BIT | RDD::BUFFER_USAGE_TRANSFER_TO_BIT, RDD::MEMORY_ALLOCATION_TYPE_GPU);
	buffer_memory += driver->buffer_get_allocation_size(p_texture->shared_fallback->buffer);

	RDG::ResourceTracker *tracker = RDG::resource_tracker_create();
	tracker->buffer_driver_id = p_texture->shared_fallback->buffer;
	p_texture->shared_fallback->buffer_tracker = tracker;
}

Vector<uint8_t> RenderingDevice::_texture_get_data(Texture *tex, uint32_t p_layer, bool p_2d) {
	uint32_t width, height, depth;
	uint32_t tight_mip_size = get_image_format_required_size(tex->format, tex->width, tex->height, p_2d ? 1 : tex->depth, tex->mipmaps, &width, &height, &depth);

	Vector<uint8_t> image_data;
	image_data.resize(tight_mip_size);

	uint32_t blockw, blockh;
	get_compressed_image_format_block_dimensions(tex->format, blockw, blockh);
	uint32_t block_size = get_compressed_image_format_block_byte_size(tex->format);
	uint32_t pixel_size = get_image_format_pixel_size(tex->format);

	{
		uint8_t *w = image_data.ptrw();

		uint32_t mipmap_offset = 0;
		for (uint32_t mm_i = 0; mm_i < tex->mipmaps; mm_i++) {
			uint32_t image_total = get_image_format_required_size(tex->format, tex->width, tex->height, p_2d ? 1 : tex->depth, mm_i + 1, &width, &height, &depth);

			uint8_t *write_ptr_mipmap = w + mipmap_offset;
			tight_mip_size = image_total - mipmap_offset;

			RDD::TextureSubresource subres;
			subres.aspect = RDD::TEXTURE_ASPECT_COLOR;
			subres.layer = p_layer;
			subres.mipmap = mm_i;
			RDD::TextureCopyableLayout layout;
			driver->texture_get_copyable_layout(tex->driver_id, subres, &layout);

			uint8_t *img_mem = driver->texture_map(tex->driver_id, subres);
			ERR_FAIL_NULL_V(img_mem, Vector<uint8_t>());

			for (uint32_t z = 0; z < depth; z++) {
				uint8_t *write_ptr = write_ptr_mipmap + z * tight_mip_size / depth;
				const uint8_t *slice_read_ptr = img_mem + z * layout.depth_pitch;

				if (block_size > 1) {
					// Compressed.
					uint32_t line_width = (block_size * (width / blockw));
					for (uint32_t y = 0; y < height / blockh; y++) {
						const uint8_t *rptr = slice_read_ptr + y * layout.row_pitch;
						uint8_t *wptr = write_ptr + y * line_width;

						memcpy(wptr, rptr, line_width);
					}

				} else {
					// Uncompressed.
					for (uint32_t y = 0; y < height; y++) {
						const uint8_t *rptr = slice_read_ptr + y * layout.row_pitch;
						uint8_t *wptr = write_ptr + y * pixel_size * width;
						memcpy(wptr, rptr, (uint64_t)pixel_size * width);
					}
				}
			}

			driver->texture_unmap(tex->driver_id);

			mipmap_offset = image_total;
		}
	}

	return image_data;
}

Vector<uint8_t> RenderingDevice::texture_get_data(RID p_texture, uint32_t p_layer) {
	ERR_RENDER_THREAD_GUARD_V(Vector<uint8_t>());

	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, Vector<uint8_t>());

	ERR_FAIL_COND_V_MSG(tex->bound, Vector<uint8_t>(),
			"Texture can't be retrieved while a draw list that uses it as part of a framebuffer is being created. Ensure the draw list is finalized (and that the color/depth texture using it is not set to `RenderingDevice.FINAL_ACTION_CONTINUE`) to retrieve this texture.");
	ERR_FAIL_COND_V_MSG(!(tex->usage_flags & TEXTURE_USAGE_CAN_COPY_FROM_BIT), Vector<uint8_t>(),
			"Texture requires the `RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT` to be set to be retrieved.");

	ERR_FAIL_COND_V(p_layer >= tex->layers, Vector<uint8_t>());

	_check_transfer_worker_texture(tex);

	if (tex->usage_flags & TEXTURE_USAGE_CPU_READ_BIT) {
		// Does not need anything fancy, map and read.
		return _texture_get_data(tex, p_layer);
	} else {
		LocalVector<RDD::TextureCopyableLayout> mip_layouts;
		// 做“纹理 ↔ 线性缓冲区”拷贝时，缓冲区起始偏移（offset）必须满足的最小字节对齐
		uint32_t work_mip_alignment = driver->api_trait_get(RDD::API_TRAIT_TEXTURE_TRANSFER_ALIGNMENT);
		uint32_t work_buffer_size = 0;
		mip_layouts.resize(tex->mipmaps);
		for (uint32_t i = 0; i < tex->mipmaps; i++) {
			RDD::TextureSubresource subres;
			subres.aspect = RDD::TEXTURE_ASPECT_COLOR;
			subres.layer = p_layer;
			subres.mipmap = i;
			driver->texture_get_copyable_layout(tex->driver_id, subres, &mip_layouts[i]);

			// Assuming layers are tightly packed. If this is not true on some driver, we must modify the copy algorithm.
			DEV_ASSERT(mip_layouts[i].layer_pitch == mip_layouts[i].size / tex->layers);

			work_buffer_size = STEPIFY(work_buffer_size, work_mip_alignment) + mip_layouts[i].size;
		}

		RDD::BufferID tmp_buffer = driver->buffer_create(work_buffer_size, RDD::BUFFER_USAGE_TRANSFER_TO_BIT, RDD::MEMORY_ALLOCATION_TYPE_CPU);
		ERR_FAIL_COND_V(!tmp_buffer, Vector<uint8_t>());

		thread_local LocalVector<RDD::BufferTextureCopyRegion> command_buffer_texture_copy_regions_vector;
		command_buffer_texture_copy_regions_vector.clear();

		uint32_t w = tex->width;
		uint32_t h = tex->height;
		uint32_t d = tex->depth;
		for (uint32_t i = 0; i < tex->mipmaps; i++) {
			RDD::BufferTextureCopyRegion copy_region;
			copy_region.buffer_offset = mip_layouts[i].offset;
			copy_region.texture_subresources.aspect = tex->read_aspect_flags;
			copy_region.texture_subresources.mipmap = i;
			copy_region.texture_subresources.base_layer = p_layer;
			copy_region.texture_subresources.layer_count = 1;
			copy_region.texture_region_size.x = w;
			copy_region.texture_region_size.y = h;
			copy_region.texture_region_size.z = d;
			command_buffer_texture_copy_regions_vector.push_back(copy_region);

			w = MAX(1u, w >> 1);
			h = MAX(1u, h >> 1);
			d = MAX(1u, d >> 1);
		}

		if (_texture_make_mutable(tex, p_texture)) {
			// The texture must be mutable to be used as a copy source due to layout transitions.
			draw_graph.add_synchronization();
		}

		draw_graph.add_texture_get_data(tex->driver_id, tex->draw_tracker, tmp_buffer, command_buffer_texture_copy_regions_vector);

		// Flush everything so memory can be safely mapped.
		// 强制刷新所有命令，这样内存就可以安全地映射出来了。
		_flush_and_stall_for_all_frames();

		const uint8_t *read_ptr = driver->buffer_map(tmp_buffer);		// 映射操作
		ERR_FAIL_NULL_V(read_ptr, Vector<uint8_t>());

		uint32_t block_w = 0;
		uint32_t block_h = 0;
		get_compressed_image_format_block_dimensions(tex->format, block_w, block_h);		// 获取当前格式下，blcok的宽高

		Vector<uint8_t> buffer_data;
		uint32_t tight_buffer_size = get_image_format_required_size(tex->format, tex->width, tex->height, tex->depth, tex->mipmaps);
		buffer_data.resize(tight_buffer_size);

		uint8_t *write_ptr = buffer_data.ptrw();

		w = tex->width;
		h = tex->height;
		d = tex->depth;
		for (uint32_t i = 0; i < tex->mipmaps; i++) {
			uint32_t width = 0, height = 0, depth = 0;
			uint32_t tight_mip_size = get_image_format_required_size(tex->format, w, h, d, 1, &width, &height, &depth);
			uint32_t tight_row_pitch = tight_mip_size / ((height / block_h) * depth);

			// Copy row-by-row to erase padding due to alignments.
			// 逐行复制，移除因为对齐而存在的padding
			const uint8_t *rp = read_ptr;
			uint8_t *wp = write_ptr;
			for (uint32_t row = h * d / block_h; row != 0; row--) {
				memcpy(wp, rp, tight_row_pitch);
				rp += mip_layouts[i].row_pitch;
				wp += tight_row_pitch;
			}

			w = MAX(block_w, w >> 1);
			h = MAX(block_h, h >> 1);
			d = MAX(1u, d >> 1);
			read_ptr += mip_layouts[i].size;
			write_ptr += tight_mip_size;
		}

		driver->buffer_unmap(tmp_buffer);		// 取消映射
		driver->buffer_free(tmp_buffer);		// 释放缓存

		return buffer_data;
	}
}

Vector<uint8_t> RenderingDevice::depth_get_data(RID p_texture, uint32_t p_layer)
{
	ERR_RENDER_THREAD_GUARD_V(Vector<uint8_t>());

	Texture* tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, Vector<uint8_t>());

	ERR_FAIL_COND_V_MSG(tex->bound, Vector<uint8_t>(),
		"Texture can't be retrieved while a draw list that uses it as part of a framebuffer is being created. Ensure the draw list is finalized (and that the color/depth texture using it is not set to `RenderingDevice.FINAL_ACTION_CONTINUE`) to retrieve this texture.");
	ERR_FAIL_COND_V_MSG(!(tex->usage_flags & TEXTURE_USAGE_CAN_COPY_FROM_BIT), Vector<uint8_t>(),
		"Texture requires the `RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT` to be set to be retrieved.");

	ERR_FAIL_COND_V(p_layer >= tex->layers, Vector<uint8_t>());

	_check_transfer_worker_texture(tex);

	if (tex->usage_flags & TEXTURE_USAGE_CPU_READ_BIT) {
		// depth buffer must not in CPU
		return Vector<uint8_t>();
	}
	else {
		LocalVector<RDD::TextureCopyableLayout> mip_layouts;
		uint32_t work_mip_alignment = driver->api_trait_get(RDD::API_TRAIT_TEXTURE_TRANSFER_ALIGNMENT);
		uint32_t work_buffer_size = 0;
		mip_layouts.resize(tex->mipmaps);

		for (uint32_t i = 0; i < tex->mipmaps; i++) {
			RDD::TextureSubresource subres;
			subres.aspect = RDD::TEXTURE_ASPECT_DEPTH;
			subres.layer = p_layer;
			subres.mipmap = i;
			driver->texture_get_copyable_layout(tex->driver_id, subres, &mip_layouts[i]);

			// Assuming layers are tightly packed. If this is not true on some driver, we must modify the copy algorithm.
			DEV_ASSERT(mip_layouts[i].layer_pitch == mip_layouts[i].size / tex->layers);

			work_buffer_size = STEPIFY(work_buffer_size, work_mip_alignment) + mip_layouts[i].size;
		}

		RDD::BufferID tmp_buffer = driver->buffer_create(work_buffer_size, RDD::BUFFER_USAGE_TRANSFER_TO_BIT, RDD::MEMORY_ALLOCATION_TYPE_CPU);
		ERR_FAIL_COND_V(!tmp_buffer, Vector<uint8_t>());

		thread_local LocalVector<RDD::BufferTextureCopyRegion> command_buffer_texture_copy_regions_vector;
		command_buffer_texture_copy_regions_vector.clear();

		uint32_t w = tex->width;
		uint32_t h = tex->height;
		uint32_t d = tex->depth;
		for (uint32_t i = 0; i < tex->mipmaps; i++) {
			RDD::BufferTextureCopyRegion copy_region;
			copy_region.buffer_offset = mip_layouts[i].offset;
			copy_region.texture_subresources.aspect = tex->read_aspect_flags;
			copy_region.texture_subresources.mipmap = i;
			copy_region.texture_subresources.base_layer = p_layer;
			copy_region.texture_subresources.layer_count = 1;
			copy_region.texture_region_size.x = w;
			copy_region.texture_region_size.y = h;
			copy_region.texture_region_size.z = d;
			command_buffer_texture_copy_regions_vector.push_back(copy_region);

			w = MAX(1u, w >> 1);
			h = MAX(1u, h >> 1);
			d = MAX(1u, d >> 1);
		}

		if (_texture_make_mutable(tex, p_texture)) {
			// The texture must be mutable to be used as a copy source due to layout transitions.
			draw_graph.add_synchronization();
		}

		draw_graph.add_texture_get_data(tex->driver_id, tex->draw_tracker, tmp_buffer, command_buffer_texture_copy_regions_vector);

		// Flush everything so memory can be safely mapped.
		// 强制刷新所有命令，这样内存就可以安全地映射出来了。
		_flush_and_stall_for_all_frames();

		const uint8_t *read_ptr = driver->buffer_map(tmp_buffer); // 映射操作
		ERR_FAIL_NULL_V(read_ptr, Vector<uint8_t>());

		uint32_t block_w = 0;
		uint32_t block_h = 0;
		get_compressed_image_format_block_dimensions(tex->format, block_w, block_h); // 获取当前格式下，blcok的宽高

		Vector<uint8_t> buffer_data;
		uint32_t tight_buffer_size = get_image_format_required_size(tex->format, tex->width, tex->height, tex->depth, tex->mipmaps);
		buffer_data.resize(tight_buffer_size);

		uint8_t *write_ptr = buffer_data.ptrw();

		w = tex->width;
		h = tex->height;
		d = tex->depth;
		for (uint32_t i = 0; i < tex->mipmaps; i++) {
			uint32_t width = 0, height = 0, depth = 0;
			uint32_t tight_mip_size = get_image_format_required_size(tex->format, w, h, d, 1, &width, &height, &depth);
			uint32_t tight_row_pitch = tight_mip_size / ((height / block_h) * depth);

			// Copy row-by-row to erase padding due to alignments.
			// 逐行复制，移除因为对齐而存在的padding
			const uint8_t *rp = read_ptr;
			uint8_t *wp = write_ptr;
			for (uint32_t row = h * d / block_h; row != 0; row--) {
				memcpy(wp, rp, tight_row_pitch);
				rp += mip_layouts[i].row_pitch;
				wp += tight_row_pitch;
			}

			w = MAX(block_w, w >> 1);
			h = MAX(block_h, h >> 1);
			d = MAX(1u, d >> 1);
			read_ptr += mip_layouts[i].size;
			write_ptr += tight_mip_size;
		}

		driver->buffer_unmap(tmp_buffer); // 取消映射
		driver->buffer_free(tmp_buffer); // 释放缓存

		return buffer_data;
	}
}

Vector<uint8_t> RenderingDevice::stencil_get_data(RID p_texture, uint32_t p_layer)
{
	ERR_RENDER_THREAD_GUARD_V(Vector<uint8_t>());

	Texture* tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, Vector<uint8_t>());

	ERR_FAIL_COND_V_MSG(tex->bound, Vector<uint8_t>(),
		"Texture can't be retrieved while a draw list that uses it as part of a framebuffer is being created. Ensure the draw list is finalized (and that the color/depth texture using it is not set to `RenderingDevice.FINAL_ACTION_CONTINUE`) to retrieve this texture.");
	ERR_FAIL_COND_V_MSG(!(tex->usage_flags & TEXTURE_USAGE_CAN_COPY_FROM_BIT), Vector<uint8_t>(),
		"Texture requires the `RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT` to be set to be retrieved.");

	ERR_FAIL_COND_V(p_layer >= tex->layers, Vector<uint8_t>());

	_check_transfer_worker_texture(tex);

	if (tex->usage_flags & TEXTURE_USAGE_CPU_READ_BIT) {
		// depth buffer must not in CPU
		return Vector<uint8_t>();
	}
	else {
		LocalVector<RDD::TextureCopyableLayout> mip_layouts;
		uint32_t work_mip_alignment = driver->api_trait_get(RDD::API_TRAIT_TEXTURE_TRANSFER_ALIGNMENT);
		uint32_t work_buffer_size = 0;
		mip_layouts.resize(tex->mipmaps);

		for (uint32_t i = 0; i < tex->mipmaps; i++) {
			RDD::TextureSubresource subres;
			subres.aspect = RDD::TEXTURE_ASPECT_STENCIL;
			subres.layer = p_layer;
			subres.mipmap = i;
			driver->texture_get_copyable_layout(tex->driver_id, subres, &mip_layouts[i]);

			// Assuming layers are tightly packed. If this is not true on some driver, we must modify the copy algorithm.
			DEV_ASSERT(mip_layouts[i].layer_pitch == mip_layouts[i].size / tex->layers);

			work_buffer_size = STEPIFY(work_buffer_size, work_mip_alignment) + mip_layouts[i].size;
		}

		RDD::BufferID tmp_buffer = driver->buffer_create(work_buffer_size, RDD::BUFFER_USAGE_TRANSFER_TO_BIT, RDD::MEMORY_ALLOCATION_TYPE_CPU);
		ERR_FAIL_COND_V(!tmp_buffer, Vector<uint8_t>());

		thread_local LocalVector<RDD::BufferTextureCopyRegion> command_buffer_texture_copy_regions_vector;
		command_buffer_texture_copy_regions_vector.clear();

		uint32_t w = tex->width;
		uint32_t h = tex->height;
		uint32_t d = tex->depth;
		for (uint32_t i = 0; i < tex->mipmaps; i++) {
			RDD::BufferTextureCopyRegion copy_region;
			copy_region.buffer_offset = mip_layouts[i].offset;
			copy_region.texture_subresources.aspect = 4;// tex->read_aspect_flags;
			copy_region.texture_subresources.mipmap = i;
			copy_region.texture_subresources.base_layer = p_layer;
			copy_region.texture_subresources.layer_count = 1;
			copy_region.texture_region_size.x = w;
			copy_region.texture_region_size.y = h;
			copy_region.texture_region_size.z = d;
			command_buffer_texture_copy_regions_vector.push_back(copy_region);

			w = MAX(1u, w >> 1);
			h = MAX(1u, h >> 1);
			d = MAX(1u, d >> 1);
		}

		if (_texture_make_mutable(tex, p_texture)) {
			// The texture must be mutable to be used as a copy source due to layout transitions.
			draw_graph.add_synchronization();
		}

		draw_graph.add_texture_get_data(tex->driver_id, tex->draw_tracker, tmp_buffer, command_buffer_texture_copy_regions_vector);

		// Flush everything so memory can be safely mapped.
		// 强制刷新所有命令，这样内存就可以安全地映射出来了。
		_flush_and_stall_for_all_frames();

		const uint8_t* read_ptr = driver->buffer_map(tmp_buffer); // 映射操作
		ERR_FAIL_NULL_V(read_ptr, Vector<uint8_t>());

		uint32_t block_w = 0;
		uint32_t block_h = 0;
		get_compressed_image_format_block_dimensions(tex->format, block_w, block_h); // 获取当前格式下，blcok的宽高

		Vector<uint8_t> buffer_data;
		uint32_t tight_buffer_size = get_image_format_required_size(tex->format, tex->width, tex->height, tex->depth, tex->mipmaps);
		buffer_data.resize(tight_buffer_size);

		uint8_t* write_ptr = buffer_data.ptrw();

		w = tex->width;
		h = tex->height;
		d = tex->depth;
		for (uint32_t i = 0; i < tex->mipmaps; i++) {
			uint32_t width = 0, height = 0, depth = 0;
			uint32_t tight_mip_size = get_image_format_required_size(tex->format, w, h, d, 1, &width, &height, &depth);
			uint32_t tight_row_pitch = tight_mip_size / ((height / block_h) * depth);

			// Copy row-by-row to erase padding due to alignments.
			// 逐行复制，移除因为对齐而存在的padding
			const uint8_t* rp = read_ptr;
			uint8_t* wp = write_ptr;
			for (uint32_t row = h * d / block_h; row != 0; row--) {
				memcpy(wp, rp, tight_row_pitch);
				rp += mip_layouts[i].row_pitch;
				wp += tight_row_pitch;
			}

			w = MAX(block_w, w >> 1);
			h = MAX(block_h, h >> 1);
			d = MAX(1u, d >> 1);
			read_ptr += mip_layouts[i].size;
			write_ptr += tight_mip_size;
		}

		driver->buffer_unmap(tmp_buffer); // 取消映射
		driver->buffer_free(tmp_buffer); // 释放缓存

		return buffer_data;
	}
}

Error RenderingDevice::texture_get_data_async(RID p_texture, uint32_t p_layer, const Callable &p_callback) {
	ERR_RENDER_THREAD_GUARD_V(ERR_UNAVAILABLE);

	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, ERR_INVALID_PARAMETER);

	ERR_FAIL_COND_V_MSG(tex->bound, ERR_INVALID_PARAMETER, "Texture can't be retrieved while a draw list that uses it as part of a framebuffer is being created. Ensure the draw list is finalized (and that the color/depth texture using it is not set to `RenderingDevice.FINAL_ACTION_CONTINUE`) to retrieve this texture.");
	ERR_FAIL_COND_V_MSG(!(tex->usage_flags & TEXTURE_USAGE_CAN_COPY_FROM_BIT), ERR_INVALID_PARAMETER, "Texture requires the `RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT` to be set to be retrieved.");
	ERR_FAIL_COND_V(p_layer >= tex->layers, ERR_INVALID_PARAMETER);

	_check_transfer_worker_texture(tex);

	thread_local LocalVector<RDD::TextureCopyableLayout> mip_layouts;
	mip_layouts.resize(tex->mipmaps);
	for (uint32_t i = 0; i < tex->mipmaps; i++) {
		RDD::TextureSubresource subres;
		subres.aspect = RDD::TEXTURE_ASPECT_COLOR;
		subres.layer = p_layer;
		subres.mipmap = i;
		driver->texture_get_copyable_layout(tex->driver_id, subres, &mip_layouts[i]);

		// Assuming layers are tightly packed. If this is not true on some driver, we must modify the copy algorithm.
		DEV_ASSERT(mip_layouts[i].layer_pitch == mip_layouts[i].size / tex->layers);
	}

	ERR_FAIL_COND_V(mip_layouts.is_empty(), ERR_INVALID_PARAMETER);

	if (_texture_make_mutable(tex, p_texture)) {
		// The texture must be mutable to be used as a copy source due to layout transitions.
		draw_graph.add_synchronization();
	}

	TextureGetDataRequest get_data_request;
	get_data_request.callback = p_callback;
	get_data_request.frame_local_index = frames[frame].download_buffer_texture_copy_regions.size();
	get_data_request.width = tex->width;
	get_data_request.height = tex->height;
	get_data_request.depth = tex->depth;
	get_data_request.format = tex->format;
	get_data_request.mipmaps = tex->mipmaps;

	uint32_t block_w, block_h;
	get_compressed_image_format_block_dimensions(tex->format, block_w, block_h);

	uint32_t pixel_size = get_image_format_pixel_size(tex->format);
	uint32_t pixel_rshift = get_compressed_image_format_pixel_rshift(tex->format);

	uint32_t w, h, d;
	uint32_t required_align = driver->api_trait_get(RDD::API_TRAIT_TEXTURE_TRANSFER_ALIGNMENT);
	uint32_t pitch_step = driver->api_trait_get(RDD::API_TRAIT_TEXTURE_DATA_ROW_PITCH_STEP);
	uint32_t region_size = texture_download_region_size_px;
	uint32_t logic_w = tex->width;
	uint32_t logic_h = tex->height;
	uint32_t mipmap_offset = 0;
	uint32_t block_write_offset;
	uint32_t block_write_amount;
	StagingRequiredAction required_action;
	uint32_t flushed_copies = 0;
	for (uint32_t i = 0; i < tex->mipmaps; i++) {
		uint32_t image_total = get_image_format_required_size(tex->format, tex->width, tex->height, tex->depth, i + 1, &w, &h, &d);
		uint32_t tight_mip_size = image_total - mipmap_offset;
		for (uint32_t z = 0; z < d; z++) {
			for (uint32_t y = 0; y < h; y += region_size) {
				for (uint32_t x = 0; x < w; x += region_size) {
					uint32_t region_w = MIN(region_size, w - x);
					uint32_t region_h = MIN(region_size, h - y);
					ERR_FAIL_COND_V(region_w % block_w, ERR_BUG);
					ERR_FAIL_COND_V(region_h % block_h, ERR_BUG);

					uint32_t region_logic_w = MIN(region_size, logic_w - x);
					uint32_t region_logic_h = MIN(region_size, logic_h - y);
					uint32_t region_pitch = (region_w * pixel_size * block_w) >> pixel_rshift;
					region_pitch = STEPIFY(region_pitch, pitch_step);

					uint32_t to_allocate = region_pitch * region_h;
					Error err = _staging_buffer_allocate(download_staging_buffers, to_allocate, required_align, block_write_offset, block_write_amount, required_action, false);
					ERR_FAIL_COND_V(err, ERR_CANT_CREATE);

					if ((get_data_request.frame_local_count > 0) && required_action == STAGING_REQUIRED_ACTION_FLUSH_AND_STALL_ALL) {
						for (uint32_t j = flushed_copies; j < get_data_request.frame_local_count; j++) {
							uint32_t local_index = get_data_request.frame_local_index + j;
							draw_graph.add_texture_get_data(tex->driver_id, tex->draw_tracker, frames[frame].download_texture_staging_buffers[local_index], frames[frame].download_buffer_texture_copy_regions[local_index]);
						}

						flushed_copies = get_data_request.frame_local_count;
					}

					_staging_buffer_execute_required_action(download_staging_buffers, required_action);

					RDD::BufferTextureCopyRegion copy_region;
					copy_region.buffer_offset = block_write_offset;
					copy_region.texture_subresources.aspect = tex->read_aspect_flags;
					copy_region.texture_subresources.mipmap = i;
					copy_region.texture_subresources.base_layer = p_layer;
					copy_region.texture_subresources.layer_count = 1;
					copy_region.texture_offset = Vector3i(x, y, z);
					copy_region.texture_region_size = Vector3i(region_logic_w, region_logic_h, 1);
					frames[frame].download_texture_staging_buffers.push_back(download_staging_buffers.blocks[download_staging_buffers.current].driver_id);
					frames[frame].download_buffer_texture_copy_regions.push_back(copy_region);
					frames[frame].download_texture_mipmap_offsets.push_back(mipmap_offset + (tight_mip_size / d) * z);
					get_data_request.frame_local_count++;

					download_staging_buffers.blocks.write[download_staging_buffers.current].fill_amount = block_write_offset + block_write_amount;
				}
			}
		}

		mipmap_offset = image_total;
		logic_w = MAX(1u, logic_w >> 1);
		logic_h = MAX(1u, logic_h >> 1);
	}

	if (get_data_request.frame_local_count > 0) {
		for (uint32_t i = flushed_copies; i < get_data_request.frame_local_count; i++) {
			uint32_t local_index = get_data_request.frame_local_index + i;
			draw_graph.add_texture_get_data(tex->driver_id, tex->draw_tracker, frames[frame].download_texture_staging_buffers[local_index], frames[frame].download_buffer_texture_copy_regions[local_index]);
		}

		flushed_copies = get_data_request.frame_local_count;
		frames[frame].download_texture_get_data_requests.push_back(get_data_request);
	}

	return OK;
}

bool RenderingDevice::texture_is_shared(RID p_texture) {
	ERR_RENDER_THREAD_GUARD_V(false);

	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, false);
	return tex->owner.is_valid();
}

bool RenderingDevice::texture_is_valid(RID p_texture) {
	ERR_RENDER_THREAD_GUARD_V(false);

	return texture_owner.owns(p_texture);
}

RD::TextureFormat RenderingDevice::texture_get_format(RID p_texture) {
	ERR_RENDER_THREAD_GUARD_V(TextureFormat());

	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, TextureFormat());

	TextureFormat tf;

	tf.format = tex->format;
	tf.width = tex->width;
	tf.height = tex->height;
	tf.depth = tex->depth;
	tf.array_layers = tex->layers;
	tf.mipmaps = tex->mipmaps;
	tf.texture_type = tex->type;
	tf.samples = tex->samples;
	tf.usage_bits = tex->usage_flags;
	tf.shareable_formats = tex->allowed_shared_formats;
	tf.is_resolve_buffer = tex->is_resolve_buffer;
	tf.is_discardable = tex->is_discardable;

	return tf;
}

Size2i RenderingDevice::texture_size(RID p_texture) {
	ERR_RENDER_THREAD_GUARD_V(Size2i());

	Texture *tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(tex, Size2i());
	return Size2i(tex->width, tex->height);
}

#ifndef DISABLE_DEPRECATED
uint64_t RenderingDevice::texture_get_native_handle(RID p_texture) {
	return get_driver_resource(DRIVER_RESOURCE_TEXTURE, p_texture);
}
#endif

Error RenderingDevice::texture_copy(RID p_from_texture, RID p_to_texture, const Vector3 &p_from, const Vector3 &p_to, const Vector3 &p_size, uint32_t p_src_mipmap, uint32_t p_dst_mipmap, uint32_t p_src_layer, uint32_t p_dst_layer) {
	ERR_RENDER_THREAD_GUARD_V(ERR_UNAVAILABLE);

	Texture *src_tex = texture_owner.get_or_null(p_from_texture);
	ERR_FAIL_NULL_V(src_tex, ERR_INVALID_PARAMETER);

	ERR_FAIL_COND_V_MSG(src_tex->bound, ERR_INVALID_PARAMETER,
			"Source texture can't be copied while a draw list that uses it as part of a framebuffer is being created. Ensure the draw list is finalized (and that the color/depth texture using it is not set to `RenderingDevice.FINAL_ACTION_CONTINUE`) to copy this texture.");
	ERR_FAIL_COND_V_MSG(!(src_tex->usage_flags & TEXTURE_USAGE_CAN_COPY_FROM_BIT), ERR_INVALID_PARAMETER,
			"Source texture requires the `RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT` to be set to be retrieved.");

	uint32_t src_width, src_height, src_depth;
	get_image_format_required_size(src_tex->format, src_tex->width, src_tex->height, src_tex->depth, p_src_mipmap + 1, &src_width, &src_height, &src_depth);

	ERR_FAIL_COND_V(p_from.x < 0 || p_from.x + p_size.x > src_width, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_from.y < 0 || p_from.y + p_size.y > src_height, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_from.z < 0 || p_from.z + p_size.z > src_depth, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_src_mipmap >= src_tex->mipmaps, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_src_layer >= src_tex->layers, ERR_INVALID_PARAMETER);

	Texture *dst_tex = texture_owner.get_or_null(p_to_texture);
	ERR_FAIL_NULL_V(dst_tex, ERR_INVALID_PARAMETER);

	ERR_FAIL_COND_V_MSG(dst_tex->bound, ERR_INVALID_PARAMETER,
			"Destination texture can't be copied while a draw list that uses it as part of a framebuffer is being created. Ensure the draw list is finalized (and that the color/depth texture using it is not set to `RenderingDevice.FINAL_ACTION_CONTINUE`) to copy this texture.");
	ERR_FAIL_COND_V_MSG(!(dst_tex->usage_flags & TEXTURE_USAGE_CAN_COPY_TO_BIT), ERR_INVALID_PARAMETER,
			"Destination texture requires the `RenderingDevice.TEXTURE_USAGE_CAN_COPY_TO_BIT` to be set to be retrieved.");

	uint32_t dst_width, dst_height, dst_depth;
	get_image_format_required_size(dst_tex->format, dst_tex->width, dst_tex->height, dst_tex->depth, p_dst_mipmap + 1, &dst_width, &dst_height, &dst_depth);

	ERR_FAIL_COND_V(p_to.x < 0 || p_to.x + p_size.x > dst_width, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_to.y < 0 || p_to.y + p_size.y > dst_height, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_to.z < 0 || p_to.z + p_size.z > dst_depth, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_dst_mipmap >= dst_tex->mipmaps, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_dst_layer >= dst_tex->layers, ERR_INVALID_PARAMETER);

	ERR_FAIL_COND_V_MSG(src_tex->read_aspect_flags != dst_tex->read_aspect_flags, ERR_INVALID_PARAMETER,
			"Source and destination texture must be of the same type (color or depth).");

	_check_transfer_worker_texture(src_tex);
	_check_transfer_worker_texture(dst_tex);

	RDD::TextureCopyRegion copy_region;
	copy_region.src_subresources.aspect = src_tex->read_aspect_flags;
	copy_region.src_subresources.mipmap = p_src_mipmap;
	copy_region.src_subresources.base_layer = p_src_layer;
	copy_region.src_subresources.layer_count = 1;
	copy_region.src_offset = p_from;

	copy_region.dst_subresources.aspect = dst_tex->read_aspect_flags;
	copy_region.dst_subresources.mipmap = p_dst_mipmap;
	copy_region.dst_subresources.base_layer = p_dst_layer;
	copy_region.dst_subresources.layer_count = 1;
	copy_region.dst_offset = p_to;

	copy_region.size = p_size;

	// Indicate the texture will get modified for the shared texture fallback.
	_texture_update_shared_fallback(p_to_texture, dst_tex, true);

	// The textures must be mutable to be used in the copy operation.
	bool src_made_mutable = _texture_make_mutable(src_tex, p_from_texture);
	bool dst_made_mutable = _texture_make_mutable(dst_tex, p_to_texture);
	if (src_made_mutable || dst_made_mutable) {
		draw_graph.add_synchronization();
	}

	draw_graph.add_texture_copy(src_tex->driver_id, src_tex->draw_tracker, dst_tex->driver_id, dst_tex->draw_tracker, copy_region);

	return OK;
}

Error RenderingDevice::texture_resolve_multisample(RID p_from_texture, RID p_to_texture) {
	ERR_RENDER_THREAD_GUARD_V(ERR_UNAVAILABLE);

	Texture *src_tex = texture_owner.get_or_null(p_from_texture);
	ERR_FAIL_NULL_V(src_tex, ERR_INVALID_PARAMETER);

	ERR_FAIL_COND_V_MSG(src_tex->bound, ERR_INVALID_PARAMETER,
			"Source texture can't be copied while a draw list that uses it as part of a framebuffer is being created. Ensure the draw list is finalized (and that the color/depth texture using it is not set to `RenderingDevice.FINAL_ACTION_CONTINUE`) to copy this texture.");
	ERR_FAIL_COND_V_MSG(!(src_tex->usage_flags & TEXTURE_USAGE_CAN_COPY_FROM_BIT), ERR_INVALID_PARAMETER,
			"Source texture requires the `RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT` to be set to be retrieved.");

	ERR_FAIL_COND_V_MSG(src_tex->type != TEXTURE_TYPE_2D, ERR_INVALID_PARAMETER, "Source texture must be 2D (or a slice of a 3D/Cube texture)");
	ERR_FAIL_COND_V_MSG(src_tex->samples == TEXTURE_SAMPLES_1, ERR_INVALID_PARAMETER, "Source texture must be multisampled.");

	Texture *dst_tex = texture_owner.get_or_null(p_to_texture);
	ERR_FAIL_NULL_V(dst_tex, ERR_INVALID_PARAMETER);

	ERR_FAIL_COND_V_MSG(dst_tex->bound, ERR_INVALID_PARAMETER,
			"Destination texture can't be copied while a draw list that uses it as part of a framebuffer is being created. Ensure the draw list is finalized (and that the color/depth texture using it is not set to `RenderingDevice.FINAL_ACTION_CONTINUE`) to copy this texture.");
	ERR_FAIL_COND_V_MSG(!(dst_tex->usage_flags & TEXTURE_USAGE_CAN_COPY_TO_BIT), ERR_INVALID_PARAMETER,
			"Destination texture requires the `RenderingDevice.TEXTURE_USAGE_CAN_COPY_TO_BIT` to be set to be retrieved.");

	ERR_FAIL_COND_V_MSG(dst_tex->type != TEXTURE_TYPE_2D, ERR_INVALID_PARAMETER, "Destination texture must be 2D (or a slice of a 3D/Cube texture).");
	ERR_FAIL_COND_V_MSG(dst_tex->samples != TEXTURE_SAMPLES_1, ERR_INVALID_PARAMETER, "Destination texture must not be multisampled.");

	ERR_FAIL_COND_V_MSG(src_tex->format != dst_tex->format, ERR_INVALID_PARAMETER, "Source and Destination textures must be the same format.");
	ERR_FAIL_COND_V_MSG(src_tex->width != dst_tex->width && src_tex->height != dst_tex->height && src_tex->depth != dst_tex->depth, ERR_INVALID_PARAMETER, "Source and Destination textures must have the same dimensions.");

	ERR_FAIL_COND_V_MSG(src_tex->read_aspect_flags != dst_tex->read_aspect_flags, ERR_INVALID_PARAMETER,
			"Source and destination texture must be of the same type (color or depth).");

	// Indicate the texture will get modified for the shared texture fallback.
	_texture_update_shared_fallback(p_to_texture, dst_tex, true);

	_check_transfer_worker_texture(src_tex);
	_check_transfer_worker_texture(dst_tex);

	// The textures must be mutable to be used in the resolve operation.
	bool src_made_mutable = _texture_make_mutable(src_tex, p_from_texture);
	bool dst_made_mutable = _texture_make_mutable(dst_tex, p_to_texture);
	if (src_made_mutable || dst_made_mutable) {
		draw_graph.add_synchronization();
	}

	draw_graph.add_texture_resolve(src_tex->driver_id, src_tex->draw_tracker, dst_tex->driver_id, dst_tex->draw_tracker, src_tex->base_layer, src_tex->base_mipmap, dst_tex->base_layer, dst_tex->base_mipmap);

	return OK;
}

void RenderingDevice::texture_set_discardable(RID p_texture, bool p_discardable) {
	ERR_RENDER_THREAD_GUARD();

	Texture *texture = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(texture);

	texture->is_discardable = p_discardable;

	if (texture->draw_tracker != nullptr) {
		texture->draw_tracker->is_discardable = p_discardable;
	}

	if (texture->shared_fallback != nullptr && texture->shared_fallback->texture_tracker != nullptr) {
		texture->shared_fallback->texture_tracker->is_discardable = p_discardable;
	}
}

bool RenderingDevice::texture_is_discardable(RID p_texture) {
	ERR_RENDER_THREAD_GUARD_V(false);

	Texture *texture = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(texture, false);

	return texture->is_discardable;
}

Error RenderingDevice::texture_clear(RID p_texture, const Color &p_color, uint32_t p_base_mipmap, uint32_t p_mipmaps, uint32_t p_base_layer, uint32_t p_layers) {
	ERR_RENDER_THREAD_GUARD_V(ERR_UNAVAILABLE);

	Texture *src_tex = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(src_tex, ERR_INVALID_PARAMETER);

	ERR_FAIL_COND_V_MSG(src_tex->bound, ERR_INVALID_PARAMETER,
			"Source texture can't be cleared while a draw list that uses it as part of a framebuffer is being created. Ensure the draw list is finalized (and that the color/depth texture using it is not set to `RenderingDevice.FINAL_ACTION_CONTINUE`) to clear this texture.");

	ERR_FAIL_COND_V(p_layers == 0, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_mipmaps == 0, ERR_INVALID_PARAMETER);

	ERR_FAIL_COND_V_MSG(!(src_tex->usage_flags & TEXTURE_USAGE_CAN_COPY_TO_BIT), ERR_INVALID_PARAMETER,
			"Source texture requires the `RenderingDevice.TEXTURE_USAGE_CAN_COPY_TO_BIT` to be set to be cleared.");

	ERR_FAIL_COND_V(p_base_mipmap + p_mipmaps > src_tex->mipmaps, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_base_layer + p_layers > src_tex->layers, ERR_INVALID_PARAMETER);

	_check_transfer_worker_texture(src_tex);

	RDD::TextureSubresourceRange range;
	range.aspect = src_tex->read_aspect_flags;
	range.base_mipmap = src_tex->base_mipmap + p_base_mipmap;
	range.mipmap_count = p_mipmaps;
	range.base_layer = src_tex->base_layer + p_base_layer;
	range.layer_count = p_layers;

	// Indicate the texture will get modified for the shared texture fallback.
	_texture_update_shared_fallback(p_texture, src_tex, true);

	if (_texture_make_mutable(src_tex, p_texture)) {
		// The texture must be mutable to be used as a clear destination.
		draw_graph.add_synchronization();
	}

	draw_graph.add_texture_clear(src_tex->driver_id, src_tex->draw_tracker, p_color, range);

	return OK;
}

bool RenderingDevice::texture_is_format_supported_for_usage(DataFormat p_format, BitField<RenderingDevice::TextureUsageBits> p_usage) const {
	ERR_FAIL_INDEX_V(p_format, DATA_FORMAT_MAX, false);

	bool cpu_readable = (p_usage & RDD::TEXTURE_USAGE_CPU_READ_BIT);
	BitField<TextureUsageBits> supported = driver->texture_get_usages_supported_by_format(p_format, cpu_readable);
	bool any_unsupported = (((int64_t)supported) | ((int64_t)p_usage)) != ((int64_t)supported);
	return !any_unsupported;
}

/*********************/
/**** FRAMEBUFFER ****/
/*********************/

RDD::RenderPassID RenderingDevice::_render_pass_create(RenderingDeviceDriver *p_driver, const Vector<AttachmentFormat> &p_attachments, const Vector<FramebufferPass> &p_passes, VectorView<RDD::AttachmentLoadOp> p_load_ops, VectorView<RDD::AttachmentStoreOp> p_store_ops, uint32_t p_view_count, Vector<TextureSamples> *r_samples) {
	// NOTE:
	// Before the refactor to RenderingDevice-RenderingDeviceDriver, there was commented out code to
	// specify dependencies to external subpasses. Since it had been unused for a long timel it wasn't ported
	// to the new architecture.

	LocalVector<int32_t> attachment_last_pass;
	attachment_last_pass.resize(p_attachments.size());

	if (p_view_count > 1) {
		const RDD::MultiviewCapabilities &capabilities = p_driver->get_multiview_capabilities();

		// This only works with multiview!
		ERR_FAIL_COND_V_MSG(!capabilities.is_supported, RDD::RenderPassID(), "Multiview not supported");

		// Make sure we limit this to the number of views we support.
		ERR_FAIL_COND_V_MSG(p_view_count > capabilities.max_view_count, RDD::RenderPassID(), "Hardware does not support requested number of views for Multiview render pass");
	}

	LocalVector<RDD::Attachment> attachments;
	LocalVector<uint32_t> attachment_remap;

	for (int i = 0; i < p_attachments.size(); i++) {
		if (p_attachments[i].usage_flags == AttachmentFormat::UNUSED_ATTACHMENT) {
			attachment_remap.push_back(RDD::AttachmentReference::UNUSED);
			continue;
		}

		ERR_FAIL_INDEX_V(p_attachments[i].format, DATA_FORMAT_MAX, RDD::RenderPassID());
		ERR_FAIL_INDEX_V(p_attachments[i].samples, TEXTURE_SAMPLES_MAX, RDD::RenderPassID());
		ERR_FAIL_COND_V_MSG(!(p_attachments[i].usage_flags & (TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | TEXTURE_USAGE_INPUT_ATTACHMENT_BIT | TEXTURE_USAGE_VRS_ATTACHMENT_BIT)),
				RDD::RenderPassID(), "Texture format for index (" + itos(i) + ") requires an attachment (color, depth-stencil, input or VRS) bit set.");

		RDD::Attachment description;
		description.format = p_attachments[i].format;
		description.samples = p_attachments[i].samples;

		// We can setup a framebuffer where we write to our VRS texture to set it up.
		// We make the assumption here that if our texture is actually used as our VRS attachment.
		// It is used as such for each subpass. This is fairly certain seeing the restrictions on subpasses.
		bool is_vrs = (p_attachments[i].usage_flags & TEXTURE_USAGE_VRS_ATTACHMENT_BIT) && i == p_passes[0].vrs_attachment;

		if (is_vrs) {
			description.load_op = RDD::ATTACHMENT_LOAD_OP_LOAD;
			description.store_op = RDD::ATTACHMENT_STORE_OP_DONT_CARE;
			description.stencil_load_op = RDD::ATTACHMENT_LOAD_OP_LOAD;
			description.stencil_store_op = RDD::ATTACHMENT_STORE_OP_DONT_CARE;
			description.initial_layout = RDD::TEXTURE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			description.final_layout = RDD::TEXTURE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		} else {
			if (p_attachments[i].usage_flags & TEXTURE_USAGE_COLOR_ATTACHMENT_BIT) {
				description.load_op = p_load_ops[i];
				description.store_op = p_store_ops[i];
				description.stencil_load_op = RDD::ATTACHMENT_LOAD_OP_DONT_CARE;
				description.stencil_store_op = RDD::ATTACHMENT_STORE_OP_DONT_CARE;
				description.initial_layout = RDD::TEXTURE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				description.final_layout = RDD::TEXTURE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			} else if (p_attachments[i].usage_flags & TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
				description.load_op = p_load_ops[i];
				description.store_op = p_store_ops[i];
				description.stencil_load_op = p_load_ops[i];
				description.stencil_store_op = p_store_ops[i];
				description.initial_layout = RDD::TEXTURE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				description.final_layout = RDD::TEXTURE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			} else {
				description.load_op = RDD::ATTACHMENT_LOAD_OP_DONT_CARE;
				description.store_op = RDD::ATTACHMENT_STORE_OP_DONT_CARE;
				description.stencil_load_op = RDD::ATTACHMENT_LOAD_OP_DONT_CARE;
				description.stencil_store_op = RDD::ATTACHMENT_STORE_OP_DONT_CARE;
				description.initial_layout = RDD::TEXTURE_LAYOUT_UNDEFINED;
				description.final_layout = RDD::TEXTURE_LAYOUT_UNDEFINED;
			}
		}

		attachment_last_pass[i] = -1;
		attachment_remap.push_back(attachments.size());
		attachments.push_back(description);
	}

	LocalVector<RDD::Subpass> subpasses;
	subpasses.resize(p_passes.size());
	LocalVector<RDD::SubpassDependency> subpass_dependencies;

	for (int i = 0; i < p_passes.size(); i++) {
		const FramebufferPass *pass = &p_passes[i];
		RDD::Subpass &subpass = subpasses[i];

		TextureSamples texture_samples = TEXTURE_SAMPLES_1;
		bool is_multisample_first = true;

		for (int j = 0; j < pass->color_attachments.size(); j++) {
			int32_t attachment = pass->color_attachments[j];
			RDD::AttachmentReference reference;
			if (attachment == ATTACHMENT_UNUSED) {
				reference.attachment = RDD::AttachmentReference::UNUSED;
				reference.layout = RDD::TEXTURE_LAYOUT_UNDEFINED;
			} else {
				ERR_FAIL_INDEX_V_MSG(attachment, p_attachments.size(), RDD::RenderPassID(), "Invalid framebuffer format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), color attachment (" + itos(j) + ").");
				ERR_FAIL_COND_V_MSG(!(p_attachments[attachment].usage_flags & TEXTURE_USAGE_COLOR_ATTACHMENT_BIT), RDD::RenderPassID(), "Invalid framebuffer format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), it's marked as depth, but it's not usable as color attachment.");
				ERR_FAIL_COND_V_MSG(attachment_last_pass[attachment] == i, RDD::RenderPassID(), "Invalid framebuffer format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), it already was used for something else before in this pass.");

				if (is_multisample_first) {
					texture_samples = p_attachments[attachment].samples;
					is_multisample_first = false;
				} else {
					ERR_FAIL_COND_V_MSG(texture_samples != p_attachments[attachment].samples, RDD::RenderPassID(), "Invalid framebuffer format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), if an attachment is marked as multisample, all of them should be multisample and use the same number of samples.");
				}
				reference.attachment = attachment_remap[attachment];
				reference.layout = RDD::TEXTURE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				attachment_last_pass[attachment] = i;
			}
			reference.aspect = RDD::TEXTURE_ASPECT_COLOR_BIT;
			subpass.color_references.push_back(reference);
		}

		for (int j = 0; j < pass->input_attachments.size(); j++) {
			int32_t attachment = pass->input_attachments[j];
			RDD::AttachmentReference reference;
			if (attachment == ATTACHMENT_UNUSED) {
				reference.attachment = RDD::AttachmentReference::UNUSED;
				reference.layout = RDD::TEXTURE_LAYOUT_UNDEFINED;
			} else {
				ERR_FAIL_INDEX_V_MSG(attachment, p_attachments.size(), RDD::RenderPassID(), "Invalid framebuffer format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), input attachment (" + itos(j) + ").");
				ERR_FAIL_COND_V_MSG(!(p_attachments[attachment].usage_flags & TEXTURE_USAGE_INPUT_ATTACHMENT_BIT), RDD::RenderPassID(), "Invalid framebuffer format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), it isn't marked as an input texture.");
				ERR_FAIL_COND_V_MSG(attachment_last_pass[attachment] == i, RDD::RenderPassID(), "Invalid framebuffer format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), it already was used for something else before in this pass.");
				reference.attachment = attachment_remap[attachment];
				reference.layout = RDD::TEXTURE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				attachment_last_pass[attachment] = i;
			}
			reference.aspect = RDD::TEXTURE_ASPECT_COLOR_BIT;
			subpass.input_references.push_back(reference);
		}

		if (pass->resolve_attachments.size() > 0) {
			ERR_FAIL_COND_V_MSG(pass->resolve_attachments.size() != pass->color_attachments.size(), RDD::RenderPassID(), "The amount of resolve attachments (" + itos(pass->resolve_attachments.size()) + ") must match the number of color attachments (" + itos(pass->color_attachments.size()) + ").");
			ERR_FAIL_COND_V_MSG(texture_samples == TEXTURE_SAMPLES_1, RDD::RenderPassID(), "Resolve attachments specified, but color attachments are not multisample.");
		}
		for (int j = 0; j < pass->resolve_attachments.size(); j++) {
			int32_t attachment = pass->resolve_attachments[j];
			attachments[attachment].load_op = RDD::ATTACHMENT_LOAD_OP_DONT_CARE;

			RDD::AttachmentReference reference;
			if (attachment == ATTACHMENT_UNUSED) {
				reference.attachment = RDD::AttachmentReference::UNUSED;
				reference.layout = RDD::TEXTURE_LAYOUT_UNDEFINED;
			} else {
				ERR_FAIL_INDEX_V_MSG(attachment, p_attachments.size(), RDD::RenderPassID(), "Invalid framebuffer format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), resolve attachment (" + itos(j) + ").");
				ERR_FAIL_COND_V_MSG(pass->color_attachments[j] == ATTACHMENT_UNUSED, RDD::RenderPassID(), "Invalid framebuffer format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), resolve attachment (" + itos(j) + "), the respective color attachment is marked as unused.");
				ERR_FAIL_COND_V_MSG(!(p_attachments[attachment].usage_flags & TEXTURE_USAGE_COLOR_ATTACHMENT_BIT), RDD::RenderPassID(), "Invalid framebuffer format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), resolve attachment, it isn't marked as a color texture.");
				ERR_FAIL_COND_V_MSG(attachment_last_pass[attachment] == i, RDD::RenderPassID(), "Invalid framebuffer format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), it already was used for something else before in this pass.");
				bool multisample = p_attachments[attachment].samples > TEXTURE_SAMPLES_1;
				ERR_FAIL_COND_V_MSG(multisample, RDD::RenderPassID(), "Invalid framebuffer format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), resolve attachments can't be multisample.");
				reference.attachment = attachment_remap[attachment];
				reference.layout = RDD::TEXTURE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // RDD::TEXTURE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				attachment_last_pass[attachment] = i;
			}
			reference.aspect = RDD::TEXTURE_ASPECT_COLOR_BIT;
			subpass.resolve_references.push_back(reference);
		}

		if (pass->depth_attachment != ATTACHMENT_UNUSED) {
			int32_t attachment = pass->depth_attachment;
			ERR_FAIL_INDEX_V_MSG(attachment, p_attachments.size(), RDD::RenderPassID(), "Invalid framebuffer depth format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), depth attachment.");
			ERR_FAIL_COND_V_MSG(!(p_attachments[attachment].usage_flags & TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT), RDD::RenderPassID(), "Invalid framebuffer depth format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), it's marked as depth, but it's not a depth attachment.");
			ERR_FAIL_COND_V_MSG(attachment_last_pass[attachment] == i, RDD::RenderPassID(), "Invalid framebuffer depth format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), it already was used for something else before in this pass.");
			subpass.depth_stencil_reference.attachment = attachment_remap[attachment];
			subpass.depth_stencil_reference.layout = RDD::TEXTURE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			attachment_last_pass[attachment] = i;

			if (is_multisample_first) {
				texture_samples = p_attachments[attachment].samples;
				is_multisample_first = false;
			} else {
				ERR_FAIL_COND_V_MSG(texture_samples != p_attachments[attachment].samples, RDD::RenderPassID(), "Invalid framebuffer depth format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), if an attachment is marked as multisample, all of them should be multisample and use the same number of samples including the depth.");
			}

		} else {
			subpass.depth_stencil_reference.attachment = RDD::AttachmentReference::UNUSED;
			subpass.depth_stencil_reference.layout = RDD::TEXTURE_LAYOUT_UNDEFINED;
		}

		if (pass->vrs_attachment != ATTACHMENT_UNUSED) {
			int32_t attachment = pass->vrs_attachment;
			ERR_FAIL_INDEX_V_MSG(attachment, p_attachments.size(), RDD::RenderPassID(), "Invalid framebuffer VRS format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), VRS attachment.");
			ERR_FAIL_COND_V_MSG(!(p_attachments[attachment].usage_flags & TEXTURE_USAGE_VRS_ATTACHMENT_BIT), RDD::RenderPassID(), "Invalid framebuffer VRS format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), it's marked as VRS, but it's not a VRS attachment.");
			ERR_FAIL_COND_V_MSG(attachment_last_pass[attachment] == i, RDD::RenderPassID(), "Invalid framebuffer VRS attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), it already was used for something else before in this pass.");

			subpass.vrs_reference.attachment = attachment_remap[attachment];
			subpass.vrs_reference.layout = RDD::TEXTURE_LAYOUT_VRS_ATTACHMENT_OPTIMAL;

			attachment_last_pass[attachment] = i;
		}

		for (int j = 0; j < pass->preserve_attachments.size(); j++) {
			int32_t attachment = pass->preserve_attachments[j];

			ERR_FAIL_COND_V_MSG(attachment == ATTACHMENT_UNUSED, RDD::RenderPassID(), "Invalid framebuffer format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), preserve attachment (" + itos(j) + "). Preserve attachments can't be unused.");

			ERR_FAIL_INDEX_V_MSG(attachment, p_attachments.size(), RDD::RenderPassID(), "Invalid framebuffer format attachment(" + itos(attachment) + "), in pass (" + itos(i) + "), preserve attachment (" + itos(j) + ").");

			if (attachment_last_pass[attachment] != i) {
				// Preserve can still be used to keep depth or color from being discarded after use.
				attachment_last_pass[attachment] = i;
				subpasses[i].preserve_attachments.push_back(attachment);
			}
		}

		if (r_samples) {
			r_samples->push_back(texture_samples);
		}

		if (i > 0) {
			RDD::SubpassDependency dependency;
			dependency.src_subpass = i - 1;
			dependency.dst_subpass = i;
			dependency.src_stages = (RDD::PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | RDD::PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | RDD::PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
			dependency.dst_stages = (RDD::PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | RDD::PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | RDD::PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | RDD::PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
			dependency.src_access = (RDD::BARRIER_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | RDD::BARRIER_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
			dependency.dst_access = (RDD::BARRIER_ACCESS_COLOR_ATTACHMENT_READ_BIT | RDD::BARRIER_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | RDD::BARRIER_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | RDD::BARRIER_ACCESS_INPUT_ATTACHMENT_READ_BIT);
			subpass_dependencies.push_back(dependency);
		}
	}

	RDD::RenderPassID render_pass = p_driver->render_pass_create(attachments, subpasses, subpass_dependencies, p_view_count);
	ERR_FAIL_COND_V(!render_pass, RDD::RenderPassID());

	return render_pass;
}

RDD::RenderPassID RenderingDevice::_render_pass_create_from_graph(RenderingDeviceDriver *p_driver, VectorView<RDD::AttachmentLoadOp> p_load_ops, VectorView<RDD::AttachmentStoreOp> p_store_ops, void *p_user_data) {
	DEV_ASSERT(p_driver != nullptr);
	DEV_ASSERT(p_user_data != nullptr);

	// The graph delegates the creation of the render pass to the user according to the load and store ops that were determined as necessary after
	// resolving the dependencies between commands. This function creates a render pass for the framebuffer accordingly.
	Framebuffer *framebuffer = (Framebuffer *)(p_user_data);
	const FramebufferFormatKey &key = framebuffer->rendering_device->framebuffer_formats[framebuffer->format_id].E->key();
	return _render_pass_create(p_driver, key.attachments, key.passes, p_load_ops, p_store_ops, framebuffer->view_count);
}

RenderingDevice::FramebufferFormatID RenderingDevice::framebuffer_format_create(const Vector<AttachmentFormat> &p_format, uint32_t p_view_count) {
	FramebufferPass pass;
	for (int i = 0; i < p_format.size(); i++) {
		if (p_format[i].usage_flags & TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			pass.depth_attachment = i;
		} else {
			pass.color_attachments.push_back(i);
		}
	}

	Vector<FramebufferPass> passes;
	passes.push_back(pass);
	return framebuffer_format_create_multipass(p_format, passes, p_view_count);
}

RenderingDevice::FramebufferFormatID RenderingDevice::framebuffer_format_create_multipass(const Vector<AttachmentFormat> &p_attachments, const Vector<FramebufferPass> &p_passes, uint32_t p_view_count) {
	_THREAD_SAFE_METHOD_

	FramebufferFormatKey key;
	key.attachments = p_attachments;
	key.passes = p_passes;
	key.view_count = p_view_count;

	const RBMap<FramebufferFormatKey, FramebufferFormatID>::Element *E = framebuffer_format_cache.find(key);
	if (E) {
		// Exists, return.
		return E->get();
	}

	Vector<TextureSamples> samples;
	LocalVector<RDD::AttachmentLoadOp> load_ops;
	LocalVector<RDD::AttachmentStoreOp> store_ops;
	for (int64_t i = 0; i < p_attachments.size(); i++) {
		load_ops.push_back(RDD::ATTACHMENT_LOAD_OP_CLEAR);
		store_ops.push_back(RDD::ATTACHMENT_STORE_OP_STORE);
	}

	RDD::RenderPassID render_pass = _render_pass_create(driver, p_attachments, p_passes, load_ops, store_ops, p_view_count, &samples); // Actions don't matter for this use case.
	if (!render_pass) { // Was likely invalid.
		return INVALID_ID;
	}

	FramebufferFormatID id = FramebufferFormatID(framebuffer_format_cache.size()) | (FramebufferFormatID(ID_TYPE_FRAMEBUFFER_FORMAT) << FramebufferFormatID(ID_BASE_SHIFT));
	E = framebuffer_format_cache.insert(key, id);

	FramebufferFormat fb_format;
	fb_format.E = E;
	fb_format.render_pass = render_pass;
	fb_format.pass_samples = samples;
	fb_format.view_count = p_view_count;
	framebuffer_formats[id] = fb_format;

#if PRINT_FRAMEBUFFER_FORMAT
	print_line("FRAMEBUFFER FORMAT:", id, "ATTACHMENTS:", p_attachments.size(), "PASSES:", p_passes.size());
	for (RD::AttachmentFormat attachment : p_attachments) {
		print_line("FORMAT:", attachment.format, "SAMPLES:", attachment.samples, "USAGE FLAGS:", attachment.usage_flags);
	}
#endif

	return id;
}

RenderingDevice::FramebufferFormatID RenderingDevice::framebuffer_format_create_empty(TextureSamples p_samples) {
	_THREAD_SAFE_METHOD_

	FramebufferFormatKey key;
	key.passes.push_back(FramebufferPass());

	const RBMap<FramebufferFormatKey, FramebufferFormatID>::Element *E = framebuffer_format_cache.find(key);
	if (E) {
		// Exists, return.
		return E->get();
	}

	LocalVector<RDD::Subpass> subpass;
	subpass.resize(1);

	RDD::RenderPassID render_pass = driver->render_pass_create({}, subpass, {}, 1);
	ERR_FAIL_COND_V(!render_pass, FramebufferFormatID());

	FramebufferFormatID id = FramebufferFormatID(framebuffer_format_cache.size()) | (FramebufferFormatID(ID_TYPE_FRAMEBUFFER_FORMAT) << FramebufferFormatID(ID_BASE_SHIFT));

	E = framebuffer_format_cache.insert(key, id);

	FramebufferFormat fb_format;
	fb_format.E = E;
	fb_format.render_pass = render_pass;
	fb_format.pass_samples.push_back(p_samples);
	framebuffer_formats[id] = fb_format;

#if PRINT_FRAMEBUFFER_FORMAT
	print_line("FRAMEBUFFER FORMAT:", id, "ATTACHMENTS: EMPTY");
#endif

	return id;
}

RenderingDevice::TextureSamples RenderingDevice::framebuffer_format_get_texture_samples(FramebufferFormatID p_format, uint32_t p_pass) {
	_THREAD_SAFE_METHOD_

	HashMap<FramebufferFormatID, FramebufferFormat>::Iterator E = framebuffer_formats.find(p_format);
	ERR_FAIL_COND_V(!E, TEXTURE_SAMPLES_1);
	ERR_FAIL_COND_V(p_pass >= uint32_t(E->value.pass_samples.size()), TEXTURE_SAMPLES_1);

	return E->value.pass_samples[p_pass];
}

RID RenderingDevice::framebuffer_create_empty(const Size2i &p_size, TextureSamples p_samples, FramebufferFormatID p_format_check) {
	_THREAD_SAFE_METHOD_

	Framebuffer framebuffer;
	framebuffer.rendering_device = this;
	framebuffer.format_id = framebuffer_format_create_empty(p_samples);
	ERR_FAIL_COND_V(p_format_check != INVALID_FORMAT_ID && framebuffer.format_id != p_format_check, RID());
	framebuffer.size = p_size;
	framebuffer.view_count = 1;

	RDG::FramebufferCache *framebuffer_cache = RDG::framebuffer_cache_create();
	framebuffer_cache->width = p_size.width;
	framebuffer_cache->height = p_size.height;
	framebuffer.framebuffer_cache = framebuffer_cache;

	RID id = framebuffer_owner.make_rid(framebuffer);
#ifdef DEV_ENABLED
	set_resource_name(id, "RID:" + itos(id.get_id()));
#endif

	framebuffer_cache->render_pass_creation_user_data = framebuffer_owner.get_or_null(id);

	return id;
}

RID RenderingDevice::framebuffer_create(const Vector<RID> &p_texture_attachments, FramebufferFormatID p_format_check, uint32_t p_view_count) {
	_THREAD_SAFE_METHOD_

	FramebufferPass pass;

	for (int i = 0; i < p_texture_attachments.size(); i++) {
		Texture *texture = texture_owner.get_or_null(p_texture_attachments[i]);

		ERR_FAIL_COND_V_MSG(texture && texture->layers != p_view_count, RID(), "Layers of our texture doesn't match view count for this framebuffer");

		if (texture != nullptr) {
			_check_transfer_worker_texture(texture);
		}

		if (texture && texture->usage_flags & TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			pass.depth_attachment = i;
		} else if (texture && texture->usage_flags & TEXTURE_USAGE_VRS_ATTACHMENT_BIT) {
			pass.vrs_attachment = i;
		} else {
			if (texture && texture->is_resolve_buffer) {
				pass.resolve_attachments.push_back(i);
			} else {
				pass.color_attachments.push_back(texture ? i : ATTACHMENT_UNUSED);
			}
		}
	}

	Vector<FramebufferPass> passes;
	passes.push_back(pass);

	return framebuffer_create_multipass(p_texture_attachments, passes, p_format_check, p_view_count);
}

RID RenderingDevice::framebuffer_create_multipass(const Vector<RID> &p_texture_attachments, const Vector<FramebufferPass> &p_passes, FramebufferFormatID p_format_check, uint32_t p_view_count) {
	_THREAD_SAFE_METHOD_

	Vector<AttachmentFormat> attachments;
	LocalVector<RDD::TextureID> textures;
	LocalVector<RDG::ResourceTracker *> trackers;
	attachments.resize(p_texture_attachments.size());
	Size2i size;
	bool size_set = false;
	for (int i = 0; i < p_texture_attachments.size(); i++) {
		AttachmentFormat af;
		Texture *texture = texture_owner.get_or_null(p_texture_attachments[i]);
		if (!texture) {
			af.usage_flags = AttachmentFormat::UNUSED_ATTACHMENT;
			trackers.push_back(nullptr);
		} else {
			ERR_FAIL_COND_V_MSG(texture->layers != p_view_count, RID(), "Layers of our texture doesn't match view count for this framebuffer");

			_check_transfer_worker_texture(texture);

			if (!size_set) {
				size.width = texture->width;
				size.height = texture->height;
				size_set = true;
			} else if (texture->usage_flags & TEXTURE_USAGE_VRS_ATTACHMENT_BIT) {
				// If this is not the first attachment we assume this is used as the VRS attachment.
				// In this case this texture will be 1/16th the size of the color attachment.
				// So we skip the size check.
			} else {
				ERR_FAIL_COND_V_MSG((uint32_t)size.width != texture->width || (uint32_t)size.height != texture->height, RID(),
						"All textures in a framebuffer should be the same size.");
			}

			af.format = texture->format;
			af.samples = texture->samples;
			af.usage_flags = texture->usage_flags;

			_texture_make_mutable(texture, p_texture_attachments[i]);

			textures.push_back(texture->driver_id);
			trackers.push_back(texture->draw_tracker);
		}
		attachments.write[i] = af;
	}

	ERR_FAIL_COND_V_MSG(!size_set, RID(), "All attachments unused.");

	FramebufferFormatID format_id = framebuffer_format_create_multipass(attachments, p_passes, p_view_count);
	if (format_id == INVALID_ID) {
		return RID();
	}

	ERR_FAIL_COND_V_MSG(p_format_check != INVALID_ID && format_id != p_format_check, RID(),
			"The format used to check this framebuffer differs from the intended framebuffer format.");

	Framebuffer framebuffer;
	framebuffer.rendering_device = this;
	framebuffer.format_id = format_id;
	framebuffer.texture_ids = p_texture_attachments;
	framebuffer.size = size;
	framebuffer.view_count = p_view_count;

	RDG::FramebufferCache *framebuffer_cache = RDG::framebuffer_cache_create();
	framebuffer_cache->width = size.width;
	framebuffer_cache->height = size.height;
	framebuffer_cache->textures = textures;
	framebuffer_cache->trackers = trackers;
	framebuffer.framebuffer_cache = framebuffer_cache;

	RID id = framebuffer_owner.make_rid(framebuffer);
#ifdef DEV_ENABLED
	set_resource_name(id, "RID:" + itos(id.get_id()));
#endif

	for (int i = 0; i < p_texture_attachments.size(); i++) {
		if (p_texture_attachments[i].is_valid()) {
			_add_dependency(id, p_texture_attachments[i]);
		}
	}

	framebuffer_cache->render_pass_creation_user_data = framebuffer_owner.get_or_null(id);

	return id;
}

RenderingDevice::FramebufferFormatID RenderingDevice::framebuffer_get_format(RID p_framebuffer) {
	_THREAD_SAFE_METHOD_

	Framebuffer *framebuffer = framebuffer_owner.get_or_null(p_framebuffer);
	ERR_FAIL_NULL_V(framebuffer, INVALID_ID);

	return framebuffer->format_id;
}

Size2 RenderingDevice::framebuffer_get_size(RID p_framebuffer) {
	_THREAD_SAFE_METHOD_

	Framebuffer *framebuffer = framebuffer_owner.get_or_null(p_framebuffer);
	ERR_FAIL_NULL_V(framebuffer, Size2(0, 0));

	return framebuffer->size;
}

bool RenderingDevice::framebuffer_is_valid(RID p_framebuffer) const {
	_THREAD_SAFE_METHOD_

	return framebuffer_owner.owns(p_framebuffer);
}

void RenderingDevice::framebuffer_set_invalidation_callback(RID p_framebuffer, InvalidationCallback p_callback, void *p_userdata) {
	_THREAD_SAFE_METHOD_

	Framebuffer *framebuffer = framebuffer_owner.get_or_null(p_framebuffer);
	ERR_FAIL_NULL(framebuffer);

	framebuffer->invalidated_callback = p_callback;
	framebuffer->invalidated_callback_userdata = p_userdata;
}

/*****************/
/**** SAMPLER ****/
/*****************/

RID RenderingDevice::sampler_create(const SamplerState &p_state) {
	_THREAD_SAFE_METHOD_

	ERR_FAIL_INDEX_V(p_state.repeat_u, SAMPLER_REPEAT_MODE_MAX, RID());
	ERR_FAIL_INDEX_V(p_state.repeat_v, SAMPLER_REPEAT_MODE_MAX, RID());
	ERR_FAIL_INDEX_V(p_state.repeat_w, SAMPLER_REPEAT_MODE_MAX, RID());
	ERR_FAIL_INDEX_V(p_state.compare_op, COMPARE_OP_MAX, RID());
	ERR_FAIL_INDEX_V(p_state.border_color, SAMPLER_BORDER_COLOR_MAX, RID());

	RDD::SamplerID sampler = driver->sampler_create(p_state);
	ERR_FAIL_COND_V(!sampler, RID());

	RID id = sampler_owner.make_rid(sampler);
#ifdef DEV_ENABLED
	set_resource_name(id, "RID:" + itos(id.get_id()));
#endif
	return id;
}

bool RenderingDevice::sampler_is_format_supported_for_filter(DataFormat p_format, SamplerFilter p_sampler_filter) const {
	_THREAD_SAFE_METHOD_

	ERR_FAIL_INDEX_V(p_format, DATA_FORMAT_MAX, false);

	return driver->sampler_is_format_supported_for_filter(p_format, p_sampler_filter);
}

/***********************/
/**** VERTEX BUFFER ****/
/***********************/

RID RenderingDevice::vertex_buffer_create(uint32_t p_size_bytes, const Vector<uint8_t> &p_data, bool p_use_as_storage) {
	ERR_FAIL_COND_V(p_data.size() && (uint32_t)p_data.size() != p_size_bytes, RID());

	Buffer buffer;
	buffer.size = p_size_bytes;
	buffer.usage = RDD::BUFFER_USAGE_TRANSFER_FROM_BIT | RDD::BUFFER_USAGE_TRANSFER_TO_BIT | RDD::BUFFER_USAGE_VERTEX_BIT;
	if (p_use_as_storage) {
		buffer.usage.set_flag(RDD::BUFFER_USAGE_STORAGE_BIT);
	}
	buffer.driver_id = driver->buffer_create(buffer.size, buffer.usage, RDD::MEMORY_ALLOCATION_TYPE_GPU);
	ERR_FAIL_COND_V(!buffer.driver_id, RID());

	// Vertex buffers are assumed to be immutable unless they don't have initial data or they've been marked for storage explicitly.
	if (p_data.is_empty() || p_use_as_storage) {
		buffer.draw_tracker = RDG::resource_tracker_create();
		buffer.draw_tracker->buffer_driver_id = buffer.driver_id;
	}

	if (p_data.size()) {
		_buffer_initialize(&buffer, p_data.ptr(), p_data.size());
	}

	_THREAD_SAFE_LOCK_
	buffer_memory += buffer.size;
	_THREAD_SAFE_UNLOCK_

	RID id = vertex_buffer_owner.make_rid(buffer);
#ifdef DEV_ENABLED
	set_resource_name(id, "RID:" + itos(id.get_id()));
#endif
	return id;
}

// Internally reference counted, this ID is warranted to be unique for the same description, but needs to be freed as many times as it was allocated.
RenderingDevice::VertexFormatID RenderingDevice::vertex_format_create(const Vector<VertexAttribute> &p_vertex_descriptions) {
	_THREAD_SAFE_METHOD_

	VertexDescriptionKey key;
	key.vertex_formats = p_vertex_descriptions;

	VertexFormatID *idptr = vertex_format_cache.getptr(key);
	if (idptr) {
		return *idptr;
	}

	HashSet<int> used_locations;
	for (int i = 0; i < p_vertex_descriptions.size(); i++) {
		ERR_CONTINUE(p_vertex_descriptions[i].format >= DATA_FORMAT_MAX);
		ERR_FAIL_COND_V(used_locations.has(p_vertex_descriptions[i].location), INVALID_ID);

		ERR_FAIL_COND_V_MSG(get_format_vertex_size(p_vertex_descriptions[i].format) == 0, INVALID_ID,
				"Data format for attachment (" + itos(i) + "), '" + FORMAT_NAMES[p_vertex_descriptions[i].format] + "', is not valid for a vertex array.");

		used_locations.insert(p_vertex_descriptions[i].location);
	}

	RDD::VertexFormatID driver_id = driver->vertex_format_create(p_vertex_descriptions);
	ERR_FAIL_COND_V(!driver_id, 0);

	VertexFormatID id = (vertex_format_cache.size() | ((int64_t)ID_TYPE_VERTEX_FORMAT << ID_BASE_SHIFT));
	vertex_format_cache[key] = id;
	vertex_formats[id].vertex_formats = p_vertex_descriptions;
	vertex_formats[id].driver_id = driver_id;
	return id;
}

RID RenderingDevice::vertex_array_create(uint32_t p_vertex_count, VertexFormatID p_vertex_format, const Vector<RID> &p_src_buffers, const Vector<uint64_t> &p_offsets) {
	_THREAD_SAFE_METHOD_

	ERR_FAIL_COND_V(!vertex_formats.has(p_vertex_format), RID());
	const VertexDescriptionCache &vd = vertex_formats[p_vertex_format];

	ERR_FAIL_COND_V(vd.vertex_formats.size() != p_src_buffers.size(), RID());

	for (int i = 0; i < p_src_buffers.size(); i++) {
		ERR_FAIL_COND_V(!vertex_buffer_owner.owns(p_src_buffers[i]), RID());
	}

	VertexArray vertex_array;

	if (p_offsets.is_empty()) {
		vertex_array.offsets.resize_zeroed(p_src_buffers.size());
	} else {
		ERR_FAIL_COND_V(p_offsets.size() != p_src_buffers.size(), RID());
		vertex_array.offsets = p_offsets;
	}

	vertex_array.vertex_count = p_vertex_count;
	vertex_array.description = p_vertex_format;
	vertex_array.max_instances_allowed = 0xFFFFFFFF; // By default as many as you want.
	for (int i = 0; i < p_src_buffers.size(); i++) {
		Buffer *buffer = vertex_buffer_owner.get_or_null(p_src_buffers[i]);

		// Validate with buffer.
		{
			const VertexAttribute &atf = vd.vertex_formats[i];

			uint32_t element_size = get_format_vertex_size(atf.format);
			ERR_FAIL_COND_V(element_size == 0, RID()); // Should never happens since this was prevalidated.

			if (atf.frequency == VERTEX_FREQUENCY_VERTEX) {
				// Validate size for regular drawing.
				uint64_t total_size = uint64_t(atf.stride) * (p_vertex_count - 1) + atf.offset + element_size;
				ERR_FAIL_COND_V_MSG(total_size > buffer->size, RID(),
						"Attachment (" + itos(i) + ") will read past the end of the buffer.");

			} else {
				// Validate size for instances drawing.
				uint64_t available = buffer->size - atf.offset;
				ERR_FAIL_COND_V_MSG(available < element_size, RID(),
						"Attachment (" + itos(i) + ") uses instancing, but it's just too small.");

				uint32_t instances_allowed = available / atf.stride;
				vertex_array.max_instances_allowed = MIN(instances_allowed, vertex_array.max_instances_allowed);
			}
		}

		vertex_array.buffers.push_back(buffer->driver_id);

		if (buffer->draw_tracker != nullptr) {
			vertex_array.draw_trackers.push_back(buffer->draw_tracker);
		} else {
			vertex_array.untracked_buffers.insert(p_src_buffers[i]);
		}

		if (buffer->transfer_worker_index >= 0) {
			vertex_array.transfer_worker_indices.push_back(buffer->transfer_worker_index);
			vertex_array.transfer_worker_operations.push_back(buffer->transfer_worker_operation);
		}
	}

	RID id = vertex_array_owner.make_rid(vertex_array);
	for (int i = 0; i < p_src_buffers.size(); i++) {
		_add_dependency(id, p_src_buffers[i]);
	}

	return id;
}

RID RenderingDevice::index_buffer_create(uint32_t p_index_count, IndexBufferFormat p_format, const Vector<uint8_t> &p_data, bool p_use_restart_indices) {
	ERR_FAIL_COND_V(p_index_count == 0, RID());

	IndexBuffer index_buffer;
	index_buffer.format = p_format;
	index_buffer.supports_restart_indices = p_use_restart_indices;
	index_buffer.index_count = p_index_count;
	uint32_t size_bytes = p_index_count * ((p_format == INDEX_BUFFER_FORMAT_UINT16) ? 2 : 4);
#ifdef DEBUG_ENABLED
	if (p_data.size()) {
		index_buffer.max_index = 0;
		ERR_FAIL_COND_V_MSG((uint32_t)p_data.size() != size_bytes, RID(),
				"Default index buffer initializer array size (" + itos(p_data.size()) + ") does not match format required size (" + itos(size_bytes) + ").");
		const uint8_t *r = p_data.ptr();
		if (p_format == INDEX_BUFFER_FORMAT_UINT16) {
			const uint16_t *index16 = (const uint16_t *)r;
			for (uint32_t i = 0; i < p_index_count; i++) {
				if (p_use_restart_indices && index16[i] == 0xFFFF) {
					continue; // Restart index, ignore.
				}
				index_buffer.max_index = MAX(index16[i], index_buffer.max_index);
			}
		} else {
			const uint32_t *index32 = (const uint32_t *)r;
			for (uint32_t i = 0; i < p_index_count; i++) {
				if (p_use_restart_indices && index32[i] == 0xFFFFFFFF) {
					continue; // Restart index, ignore.
				}
				index_buffer.max_index = MAX(index32[i], index_buffer.max_index);
			}
		}
	} else {
		index_buffer.max_index = 0xFFFFFFFF;
	}
#else
	index_buffer.max_index = 0xFFFFFFFF;
#endif
	index_buffer.size = size_bytes;
	index_buffer.usage = (RDD::BUFFER_USAGE_TRANSFER_FROM_BIT | RDD::BUFFER_USAGE_TRANSFER_TO_BIT | RDD::BUFFER_USAGE_INDEX_BIT);
	index_buffer.driver_id = driver->buffer_create(index_buffer.size, index_buffer.usage, RDD::MEMORY_ALLOCATION_TYPE_GPU);
	ERR_FAIL_COND_V(!index_buffer.driver_id, RID());

	// Index buffers are assumed to be immutable unless they don't have initial data.
	if (p_data.is_empty()) {
		index_buffer.draw_tracker = RDG::resource_tracker_create();
		index_buffer.draw_tracker->buffer_driver_id = index_buffer.driver_id;
	}

	if (p_data.size()) {
		_buffer_initialize(&index_buffer, p_data.ptr(), p_data.size());
	}

	_THREAD_SAFE_LOCK_
	buffer_memory += index_buffer.size;
	_THREAD_SAFE_UNLOCK_

	RID id = index_buffer_owner.make_rid(index_buffer);
#ifdef DEV_ENABLED
	set_resource_name(id, "RID:" + itos(id.get_id()));
#endif
	return id;
}

RID RenderingDevice::index_array_create(RID p_index_buffer, uint32_t p_index_offset, uint32_t p_index_count) {
	_THREAD_SAFE_METHOD_

	ERR_FAIL_COND_V(!index_buffer_owner.owns(p_index_buffer), RID());

	IndexBuffer *index_buffer = index_buffer_owner.get_or_null(p_index_buffer);

	ERR_FAIL_COND_V(p_index_count == 0, RID());
	ERR_FAIL_COND_V(p_index_offset + p_index_count > index_buffer->index_count, RID());

	IndexArray index_array;
	index_array.max_index = index_buffer->max_index;
	index_array.driver_id = index_buffer->driver_id;
	index_array.draw_tracker = index_buffer->draw_tracker;
	index_array.offset = p_index_offset;
	index_array.indices = p_index_count;
	index_array.format = index_buffer->format;
	index_array.supports_restart_indices = index_buffer->supports_restart_indices;
	index_array.transfer_worker_index = index_buffer->transfer_worker_index;
	index_array.transfer_worker_operation = index_buffer->transfer_worker_operation;

	RID id = index_array_owner.make_rid(index_array);
	_add_dependency(id, p_index_buffer);
	return id;
}

/****************/
/**** SHADER ****/
/****************/

static const char *SHADER_UNIFORM_NAMES[RenderingDevice::UNIFORM_TYPE_MAX] = {
	"Sampler", "CombinedSampler", "Texture", "Image", "TextureBuffer", "SamplerTextureBuffer", "ImageBuffer", "UniformBuffer", "StorageBuffer", "InputAttachment"
};

String RenderingDevice::_shader_uniform_debug(RID p_shader, int p_set) {
	String ret;
	const Shader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL_V(shader, String());
	for (int i = 0; i < shader->uniform_sets.size(); i++) {
		if (p_set >= 0 && i != p_set) {
			continue;
		}
		for (int j = 0; j < shader->uniform_sets[i].size(); j++) {
			const ShaderUniform &ui = shader->uniform_sets[i][j];
			if (!ret.is_empty()) {
				ret += "\n";
			}
			ret += "Set: " + itos(i) + " Binding: " + itos(ui.binding) + " Type: " + SHADER_UNIFORM_NAMES[ui.type] + " Writable: " + (ui.writable ? "Y" : "N") + " Length: " + itos(ui.length);
		}
	}
	return ret;
}

String RenderingDevice::shader_get_binary_cache_key() const {
	return driver->shader_get_binary_cache_key();
}

Vector<uint8_t> RenderingDevice::shader_compile_binary_from_spirv(const Vector<ShaderStageSPIRVData> &p_spirv, const String &p_shader_name) {
	return driver->shader_compile_binary_from_spirv(p_spirv, p_shader_name);
}

RID RenderingDevice::shader_create_from_bytecode(const Vector<uint8_t> &p_shader_binary, RID p_placeholder) {
	// Immutable samplers :
	// Expanding api when creating shader to allow passing optionally a set of immutable samplers
	// keeping existing api but extending it by sending an empty set.
	Vector<PipelineImmutableSampler> immutable_samplers;
	return shader_create_from_bytecode_with_samplers(p_shader_binary, p_placeholder, immutable_samplers);
}

RID RenderingDevice::shader_create_from_bytecode_with_samplers(const Vector<uint8_t> &p_shader_binary, RID p_placeholder, const Vector<PipelineImmutableSampler> &p_immutable_samplers) {
	_THREAD_SAFE_METHOD_

	ShaderDescription shader_desc;
	String name;

	Vector<RDD::ImmutableSampler> driver_immutable_samplers;
	for (const PipelineImmutableSampler &source_sampler : p_immutable_samplers) {
		RDD::ImmutableSampler driver_sampler;
		driver_sampler.type = source_sampler.uniform_type;
		driver_sampler.binding = source_sampler.binding;

		for (uint32_t j = 0; j < source_sampler.get_id_count(); j++) {
			RDD::SamplerID *sampler_driver_id = sampler_owner.get_or_null(source_sampler.get_id(j));
			driver_sampler.ids.push_back(*sampler_driver_id);
		}

		driver_immutable_samplers.append(driver_sampler);
	}
	RDD::ShaderID shader_id = driver->shader_create_from_bytecode(p_shader_binary, shader_desc, name, driver_immutable_samplers);
	ERR_FAIL_COND_V(!shader_id, RID());

	// All good, let's create modules.

	RID id;
	if (p_placeholder.is_null()) {
		id = shader_owner.make_rid();
	} else {
		id = p_placeholder;
	}

	Shader *shader = shader_owner.get_or_null(id);
	ERR_FAIL_NULL_V(shader, RID());

	*((ShaderDescription *)shader) = shader_desc; // ShaderDescription bundle.
	shader->name = name;
	shader->driver_id = shader_id;
	shader->layout_hash = driver->shader_get_layout_hash(shader_id);

	for (int i = 0; i < shader->uniform_sets.size(); i++) {
		uint32_t format = 0; // No format, default.

		if (shader->uniform_sets[i].size()) {
			// Sort and hash.

			shader->uniform_sets.write[i].sort();

			UniformSetFormat usformat;
			usformat.uniforms = shader->uniform_sets[i];
			RBMap<UniformSetFormat, uint32_t>::Element *E = uniform_set_format_cache.find(usformat);
			if (E) {
				format = E->get();
			} else {
				format = uniform_set_format_cache.size() + 1;
				uniform_set_format_cache.insert(usformat, format);
			}
		}

		shader->set_formats.push_back(format);
	}

	for (ShaderStage stage : shader_desc.stages) {
		switch (stage) {
			case SHADER_STAGE_VERTEX:
				shader->stage_bits.set_flag(RDD::PIPELINE_STAGE_VERTEX_SHADER_BIT);
				break;
			case SHADER_STAGE_FRAGMENT:
				shader->stage_bits.set_flag(RDD::PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
				break;
			case SHADER_STAGE_TESSELATION_CONTROL:
				shader->stage_bits.set_flag(RDD::PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT);
				break;
			case SHADER_STAGE_TESSELATION_EVALUATION:
				shader->stage_bits.set_flag(RDD::PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT);
				break;
			case SHADER_STAGE_COMPUTE:
				shader->stage_bits.set_flag(RDD::PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				break;
			default:
				DEV_ASSERT(false && "Unknown shader stage.");
				break;
		}
	}

#ifdef DEV_ENABLED
	set_resource_name(id, "RID:" + itos(id.get_id()));
#endif
	return id;
}

void RenderingDevice::shader_destroy_modules(RID p_shader) {
	Shader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL(shader);
	driver->shader_destroy_modules(shader->driver_id);
}

RID RenderingDevice::shader_create_placeholder() {
	_THREAD_SAFE_METHOD_

	Shader shader;
	return shader_owner.make_rid(shader);
}

uint64_t RenderingDevice::shader_get_vertex_input_attribute_mask(RID p_shader) {
	_THREAD_SAFE_METHOD_

	const Shader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL_V(shader, 0);
	return shader->vertex_input_mask;
}

/******************/
/**** UNIFORMS ****/
/******************/

RID RenderingDevice::uniform_buffer_create(uint32_t p_size_bytes, const Vector<uint8_t> &p_data) {
	ERR_FAIL_COND_V(p_data.size() && (uint32_t)p_data.size() != p_size_bytes, RID());

	Buffer buffer;
	buffer.size = p_size_bytes;
	buffer.usage = (RDD::BUFFER_USAGE_TRANSFER_TO_BIT | RDD::BUFFER_USAGE_UNIFORM_BIT);
	buffer.driver_id = driver->buffer_create(buffer.size, buffer.usage, RDD::MEMORY_ALLOCATION_TYPE_GPU);
	ERR_FAIL_COND_V(!buffer.driver_id, RID());

	// Uniform buffers are assumed to be immutable unless they don't have initial data.
	if (p_data.is_empty()) {
		buffer.draw_tracker = RDG::resource_tracker_create();
		buffer.draw_tracker->buffer_driver_id = buffer.driver_id;
	}

	if (p_data.size()) {
		_buffer_initialize(&buffer, p_data.ptr(), p_data.size());
	}

	_THREAD_SAFE_LOCK_
	buffer_memory += buffer.size;
	_THREAD_SAFE_UNLOCK_

	RID id = uniform_buffer_owner.make_rid(buffer);
#ifdef DEV_ENABLED
	set_resource_name(id, "RID:" + itos(id.get_id()));
#endif
	return id;
}

void RenderingDevice::_uniform_set_update_shared(UniformSet *p_uniform_set) {
	for (UniformSet::SharedTexture shared : p_uniform_set->shared_textures_to_update) {
		Texture *texture = texture_owner.get_or_null(shared.texture);
		ERR_CONTINUE(texture == nullptr);
		_texture_update_shared_fallback(shared.texture, texture, shared.writing);
	}
}

template RID RenderingDevice::uniform_set_create(const LocalVector<RD::Uniform> &p_uniforms, RID p_shader, uint32_t p_shader_set, bool p_linear_pool);

template RID RenderingDevice::uniform_set_create(const Vector<RD::Uniform> &p_uniforms, RID p_shader, uint32_t p_shader_set, bool p_linear_pool);

template <typename Collection>
RID RenderingDevice::uniform_set_create(const Collection &p_uniforms, RID p_shader, uint32_t p_shader_set, bool p_linear_pool) {
	// 线程安全保护
	_THREAD_SAFE_METHOD_

	ERR_FAIL_COND_V(p_uniforms.is_empty(), RID());

	// 获取真正的shader对象
	Shader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL_V(shader, RID());

	ERR_FAIL_COND_V_MSG(p_shader_set >= (uint32_t)shader->uniform_sets.size() || shader->uniform_sets[p_shader_set].is_empty(), RID(),
			"Desired set (" + itos(p_shader_set) + ") not used by shader.");
	// See that all sets in shader are satisfied.

	const Vector<ShaderUniform> &set = shader->uniform_sets[p_shader_set];

	uint32_t uniform_count = p_uniforms.size();
	const Uniform *uniforms = p_uniforms.ptr();

	uint32_t set_uniform_count = set.size();
	const ShaderUniform *set_uniforms = set.ptr();

	LocalVector<RDD::BoundUniform> driver_uniforms;
	driver_uniforms.resize(set_uniform_count);

	// Used for verification to make sure a uniform set does not use a framebuffer bound texture.
	LocalVector<UniformSet::AttachableTexture> attachable_textures;
	Vector<RDG::ResourceTracker *> draw_trackers;
	Vector<RDG::ResourceUsage> draw_trackers_usage;
	HashMap<RID, RDG::ResourceUsage> untracked_usage;
	Vector<UniformSet::SharedTexture> shared_textures_to_update;

	for (uint32_t i = 0; i < set_uniform_count; i++) {
		const ShaderUniform &set_uniform = set_uniforms[i];
		int uniform_idx = -1;
		for (int j = 0; j < (int)uniform_count; j++) {
			if (uniforms[j].binding == set_uniform.binding) {
				uniform_idx = j;
				break;
			}
		}
		ERR_FAIL_COND_V_MSG(uniform_idx == -1, RID(),
				"All the shader bindings for the given set must be covered by the uniforms provided. Binding (" + itos(set_uniform.binding) + "), set (" + itos(p_shader_set) + ") was not provided.");

		const Uniform &uniform = uniforms[uniform_idx];

		ERR_FAIL_COND_V_MSG(uniform.uniform_type != set_uniform.type, RID(),
				"Mismatch uniform type for binding (" + itos(set_uniform.binding) + "), set (" + itos(p_shader_set) + "). Expected '" + SHADER_UNIFORM_NAMES[set_uniform.type] + "', supplied: '" + SHADER_UNIFORM_NAMES[uniform.uniform_type] + "'.");

		RDD::BoundUniform &driver_uniform = driver_uniforms[i];
		driver_uniform.type = uniform.uniform_type;
		driver_uniform.binding = uniform.binding;

		// Mark immutable samplers to be skipped when creating uniform set.
		driver_uniform.immutable_sampler = uniform.immutable_sampler;

		switch (uniform.uniform_type) {
			case UNIFORM_TYPE_SAMPLER: {
				if (uniform.get_id_count() != (uint32_t)set_uniform.length) {
					if (set_uniform.length > 1) {
						ERR_FAIL_V_MSG(RID(), "Sampler (binding: " + itos(uniform.binding) + ") is an array of (" + itos(set_uniform.length) + ") sampler elements, so it should be provided equal number of sampler IDs to satisfy it (IDs provided: " + itos(uniform.get_id_count()) + ").");
					} else {
						ERR_FAIL_V_MSG(RID(), "Sampler (binding: " + itos(uniform.binding) + ") should provide one ID referencing a sampler (IDs provided: " + itos(uniform.get_id_count()) + ").");
					}
				}

				for (uint32_t j = 0; j < uniform.get_id_count(); j++) {
					RDD::SamplerID *sampler_driver_id = sampler_owner.get_or_null(uniform.get_id(j));
					ERR_FAIL_NULL_V_MSG(sampler_driver_id, RID(), "Sampler (binding: " + itos(uniform.binding) + ", index " + itos(j) + ") is not a valid sampler.");

					driver_uniform.ids.push_back(*sampler_driver_id);
				}
			} break;
			case UNIFORM_TYPE_SAMPLER_WITH_TEXTURE: {
				if (uniform.get_id_count() != (uint32_t)set_uniform.length * 2) {
					if (set_uniform.length > 1) {
						ERR_FAIL_V_MSG(RID(), "SamplerTexture (binding: " + itos(uniform.binding) + ") is an array of (" + itos(set_uniform.length) + ") sampler&texture elements, so it should provided twice the amount of IDs (sampler,texture pairs) to satisfy it (IDs provided: " + itos(uniform.get_id_count()) + ").");
					} else {
						ERR_FAIL_V_MSG(RID(), "SamplerTexture (binding: " + itos(uniform.binding) + ") should provide two IDs referencing a sampler and then a texture (IDs provided: " + itos(uniform.get_id_count()) + ").");
					}
				}

				for (uint32_t j = 0; j < uniform.get_id_count(); j += 2) {
					RDD::SamplerID *sampler_driver_id = sampler_owner.get_or_null(uniform.get_id(j + 0));
					ERR_FAIL_NULL_V_MSG(sampler_driver_id, RID(), "SamplerBuffer (binding: " + itos(uniform.binding) + ", index " + itos(j + 1) + ") is not a valid sampler.");

					RID texture_id = uniform.get_id(j + 1);
					Texture *texture = texture_owner.get_or_null(texture_id);
					ERR_FAIL_NULL_V_MSG(texture, RID(), "Texture (binding: " + itos(uniform.binding) + ", index " + itos(j) + ") is not a valid texture.");

					ERR_FAIL_COND_V_MSG(!(texture->usage_flags & TEXTURE_USAGE_SAMPLING_BIT), RID(),
							"Texture (binding: " + itos(uniform.binding) + ", index " + itos(j) + ") needs the TEXTURE_USAGE_SAMPLING_BIT usage flag set in order to be used as uniform.");

					if ((texture->usage_flags & (TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | TEXTURE_USAGE_INPUT_ATTACHMENT_BIT))) {
						UniformSet::AttachableTexture attachable_texture;
						attachable_texture.bind = set_uniform.binding;
						attachable_texture.texture = texture->owner.is_valid() ? texture->owner : uniform.get_id(j + 1);
						attachable_textures.push_back(attachable_texture);
					}

					RDD::TextureID driver_id = texture->driver_id;
					RDG::ResourceTracker *tracker = texture->draw_tracker;
					if (texture->shared_fallback != nullptr && texture->shared_fallback->texture.id != 0) {
						driver_id = texture->shared_fallback->texture;
						tracker = texture->shared_fallback->texture_tracker;
						shared_textures_to_update.push_back({ false, texture_id });
					}

					if (tracker != nullptr) {
						draw_trackers.push_back(tracker);
						draw_trackers_usage.push_back(RDG::RESOURCE_USAGE_TEXTURE_SAMPLE);
					} else {
						untracked_usage[texture_id] = RDG::RESOURCE_USAGE_TEXTURE_SAMPLE;
					}

					DEV_ASSERT(!texture->owner.is_valid() || texture_owner.get_or_null(texture->owner));

					driver_uniform.ids.push_back(*sampler_driver_id);
					driver_uniform.ids.push_back(driver_id);
					_check_transfer_worker_texture(texture);
				}
			} break;
			case UNIFORM_TYPE_TEXTURE: {
				if (uniform.get_id_count() != (uint32_t)set_uniform.length) {
					if (set_uniform.length > 1) {
						ERR_FAIL_V_MSG(RID(), "Texture (binding: " + itos(uniform.binding) + ") is an array of (" + itos(set_uniform.length) + ") textures, so it should be provided equal number of texture IDs to satisfy it (IDs provided: " + itos(uniform.get_id_count()) + ").");
					} else {
						ERR_FAIL_V_MSG(RID(), "Texture (binding: " + itos(uniform.binding) + ") should provide one ID referencing a texture (IDs provided: " + itos(uniform.get_id_count()) + ").");
					}
				}

				for (uint32_t j = 0; j < uniform.get_id_count(); j++) {
					RID texture_id = uniform.get_id(j);
					Texture *texture = texture_owner.get_or_null(texture_id);
					ERR_FAIL_NULL_V_MSG(texture, RID(), "Texture (binding: " + itos(uniform.binding) + ", index " + itos(j) + ") is not a valid texture.");

					ERR_FAIL_COND_V_MSG(!(texture->usage_flags & TEXTURE_USAGE_SAMPLING_BIT), RID(),
							"Texture (binding: " + itos(uniform.binding) + ", index " + itos(j) + ") needs the TEXTURE_USAGE_SAMPLING_BIT usage flag set in order to be used as uniform.");

					if ((texture->usage_flags & (TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | TEXTURE_USAGE_INPUT_ATTACHMENT_BIT))) {
						UniformSet::AttachableTexture attachable_texture;
						attachable_texture.bind = set_uniform.binding;
						attachable_texture.texture = texture->owner.is_valid() ? texture->owner : uniform.get_id(j);
						attachable_textures.push_back(attachable_texture);
					}

					RDD::TextureID driver_id = texture->driver_id;
					RDG::ResourceTracker *tracker = texture->draw_tracker;
					if (texture->shared_fallback != nullptr && texture->shared_fallback->texture.id != 0) {
						driver_id = texture->shared_fallback->texture;
						tracker = texture->shared_fallback->texture_tracker;
						shared_textures_to_update.push_back({ false, texture_id });
					}

					if (tracker != nullptr) {
						draw_trackers.push_back(tracker);
						draw_trackers_usage.push_back(RDG::RESOURCE_USAGE_TEXTURE_SAMPLE);
					} else {
						untracked_usage[texture_id] = RDG::RESOURCE_USAGE_TEXTURE_SAMPLE;
					}

					DEV_ASSERT(!texture->owner.is_valid() || texture_owner.get_or_null(texture->owner));

					driver_uniform.ids.push_back(driver_id);
					_check_transfer_worker_texture(texture);
				}
			} break;
			case UNIFORM_TYPE_IMAGE: {
				if (uniform.get_id_count() != (uint32_t)set_uniform.length) {
					if (set_uniform.length > 1) {
						ERR_FAIL_V_MSG(RID(), "Image (binding: " + itos(uniform.binding) + ") is an array of (" + itos(set_uniform.length) + ") textures, so it should be provided equal number of texture IDs to satisfy it (IDs provided: " + itos(uniform.get_id_count()) + ").");
					} else {
						ERR_FAIL_V_MSG(RID(), "Image (binding: " + itos(uniform.binding) + ") should provide one ID referencing a texture (IDs provided: " + itos(uniform.get_id_count()) + ").");
					}
				}

				for (uint32_t j = 0; j < uniform.get_id_count(); j++) {
					RID texture_id = uniform.get_id(j);
					Texture *texture = texture_owner.get_or_null(texture_id);

					ERR_FAIL_NULL_V_MSG(texture, RID(),
							"Image (binding: " + itos(uniform.binding) + ", index " + itos(j) + ") is not a valid texture.");

					ERR_FAIL_COND_V_MSG(!(texture->usage_flags & TEXTURE_USAGE_STORAGE_BIT), RID(),
							"Image (binding: " + itos(uniform.binding) + ", index " + itos(j) + ") needs the TEXTURE_USAGE_STORAGE_BIT usage flag set in order to be used as uniform.");

					if (texture->owner.is_null() && texture->shared_fallback != nullptr) {
						shared_textures_to_update.push_back({ true, texture_id });
					}

					if (_texture_make_mutable(texture, texture_id)) {
						// The texture must be mutable as a layout transition will be required.
						draw_graph.add_synchronization();
					}

					if (texture->draw_tracker != nullptr) {
						draw_trackers.push_back(texture->draw_tracker);

						if (set_uniform.writable) {
							draw_trackers_usage.push_back(RDG::RESOURCE_USAGE_STORAGE_IMAGE_READ_WRITE);
						} else {
							draw_trackers_usage.push_back(RDG::RESOURCE_USAGE_STORAGE_IMAGE_READ);
						}
					}

					DEV_ASSERT(!texture->owner.is_valid() || texture_owner.get_or_null(texture->owner));

					driver_uniform.ids.push_back(texture->driver_id);
					_check_transfer_worker_texture(texture);
				}
			} break;
			case UNIFORM_TYPE_TEXTURE_BUFFER: {
				if (uniform.get_id_count() != (uint32_t)set_uniform.length) {
					if (set_uniform.length > 1) {
						ERR_FAIL_V_MSG(RID(), "Buffer (binding: " + itos(uniform.binding) + ") is an array of (" + itos(set_uniform.length) + ") texture buffer elements, so it should be provided equal number of texture buffer IDs to satisfy it (IDs provided: " + itos(uniform.get_id_count()) + ").");
					} else {
						ERR_FAIL_V_MSG(RID(), "Buffer (binding: " + itos(uniform.binding) + ") should provide one ID referencing a texture buffer (IDs provided: " + itos(uniform.get_id_count()) + ").");
					}
				}

				for (uint32_t j = 0; j < uniform.get_id_count(); j++) {
					RID buffer_id = uniform.get_id(j);
					Buffer *buffer = texture_buffer_owner.get_or_null(buffer_id);
					ERR_FAIL_NULL_V_MSG(buffer, RID(), "Texture Buffer (binding: " + itos(uniform.binding) + ", index " + itos(j) + ") is not a valid texture buffer.");

					if (set_uniform.writable && _buffer_make_mutable(buffer, buffer_id)) {
						// The buffer must be mutable if it's used for writing.
						draw_graph.add_synchronization();
					}

					if (buffer->draw_tracker != nullptr) {
						draw_trackers.push_back(buffer->draw_tracker);

						if (set_uniform.writable) {
							draw_trackers_usage.push_back(RDG::RESOURCE_USAGE_TEXTURE_BUFFER_READ_WRITE);
						} else {
							draw_trackers_usage.push_back(RDG::RESOURCE_USAGE_TEXTURE_BUFFER_READ);
						}
					} else {
						untracked_usage[buffer_id] = RDG::RESOURCE_USAGE_TEXTURE_BUFFER_READ;
					}

					driver_uniform.ids.push_back(buffer->driver_id);
					_check_transfer_worker_buffer(buffer);
				}
			} break;
			case UNIFORM_TYPE_SAMPLER_WITH_TEXTURE_BUFFER: {
				if (uniform.get_id_count() != (uint32_t)set_uniform.length * 2) {
					if (set_uniform.length > 1) {
						ERR_FAIL_V_MSG(RID(), "SamplerBuffer (binding: " + itos(uniform.binding) + ") is an array of (" + itos(set_uniform.length) + ") sampler buffer elements, so it should provided twice the amount of IDs (sampler,buffer pairs) to satisfy it (IDs provided: " + itos(uniform.get_id_count()) + ").");
					} else {
						ERR_FAIL_V_MSG(RID(), "SamplerBuffer (binding: " + itos(uniform.binding) + ") should provide two IDs referencing a sampler and then a texture buffer (IDs provided: " + itos(uniform.get_id_count()) + ").");
					}
				}

				for (uint32_t j = 0; j < uniform.get_id_count(); j += 2) {
					RDD::SamplerID *sampler_driver_id = sampler_owner.get_or_null(uniform.get_id(j + 0));
					ERR_FAIL_NULL_V_MSG(sampler_driver_id, RID(), "SamplerBuffer (binding: " + itos(uniform.binding) + ", index " + itos(j + 1) + ") is not a valid sampler.");

					RID buffer_id = uniform.get_id(j + 1);
					Buffer *buffer = texture_buffer_owner.get_or_null(buffer_id);
					ERR_FAIL_NULL_V_MSG(buffer, RID(), "SamplerBuffer (binding: " + itos(uniform.binding) + ", index " + itos(j + 1) + ") is not a valid texture buffer.");

					if (buffer->draw_tracker != nullptr) {
						draw_trackers.push_back(buffer->draw_tracker);
						draw_trackers_usage.push_back(RDG::RESOURCE_USAGE_TEXTURE_BUFFER_READ);
					} else {
						untracked_usage[buffer_id] = RDG::RESOURCE_USAGE_TEXTURE_BUFFER_READ;
					}

					driver_uniform.ids.push_back(*sampler_driver_id);
					driver_uniform.ids.push_back(buffer->driver_id);
					_check_transfer_worker_buffer(buffer);
				}
			} break;
			case UNIFORM_TYPE_IMAGE_BUFFER: {
				// Todo.
			} break;
			case UNIFORM_TYPE_UNIFORM_BUFFER: {
				ERR_FAIL_COND_V_MSG(uniform.get_id_count() != 1, RID(),
						"Uniform buffer supplied (binding: " + itos(uniform.binding) + ") must provide one ID (" + itos(uniform.get_id_count()) + " provided).");

				RID buffer_id = uniform.get_id(0);
				Buffer *buffer = uniform_buffer_owner.get_or_null(buffer_id);
				ERR_FAIL_NULL_V_MSG(buffer, RID(), "Uniform buffer supplied (binding: " + itos(uniform.binding) + ") is invalid.");

				ERR_FAIL_COND_V_MSG(buffer->size < (uint32_t)set_uniform.length, RID(),
						"Uniform buffer supplied (binding: " + itos(uniform.binding) + ") size (" + itos(buffer->size) + ") is smaller than size of shader uniform: (" + itos(set_uniform.length) + ").");

				if (buffer->draw_tracker != nullptr) {
					draw_trackers.push_back(buffer->draw_tracker);
					draw_trackers_usage.push_back(RDG::RESOURCE_USAGE_UNIFORM_BUFFER_READ);
				} else {
					untracked_usage[buffer_id] = RDG::RESOURCE_USAGE_UNIFORM_BUFFER_READ;
				}

				driver_uniform.ids.push_back(buffer->driver_id);
				_check_transfer_worker_buffer(buffer);
			} break;
			case UNIFORM_TYPE_STORAGE_BUFFER: {
				ERR_FAIL_COND_V_MSG(uniform.get_id_count() != 1, RID(),
						"Storage buffer supplied (binding: " + itos(uniform.binding) + ") must provide one ID (" + itos(uniform.get_id_count()) + " provided).");

				Buffer *buffer = nullptr;

				RID buffer_id = uniform.get_id(0);
				if (storage_buffer_owner.owns(buffer_id)) {
					buffer = storage_buffer_owner.get_or_null(buffer_id);
				} else if (vertex_buffer_owner.owns(buffer_id)) {
					buffer = vertex_buffer_owner.get_or_null(buffer_id);

					ERR_FAIL_COND_V_MSG(!(buffer->usage.has_flag(RDD::BUFFER_USAGE_STORAGE_BIT)), RID(), "Vertex buffer supplied (binding: " + itos(uniform.binding) + ") was not created with storage flag.");
				}
				ERR_FAIL_NULL_V_MSG(buffer, RID(), "Storage buffer supplied (binding: " + itos(uniform.binding) + ") is invalid.");

				// If 0, then it's sized on link time.
				ERR_FAIL_COND_V_MSG(set_uniform.length > 0 && buffer->size != (uint32_t)set_uniform.length, RID(),
						"Storage buffer supplied (binding: " + itos(uniform.binding) + ") size (" + itos(buffer->size) + ") does not match size of shader uniform: (" + itos(set_uniform.length) + ").");

				if (set_uniform.writable && _buffer_make_mutable(buffer, buffer_id)) {
					// The buffer must be mutable if it's used for writing.
					draw_graph.add_synchronization();
				}

				if (buffer->draw_tracker != nullptr) {
					draw_trackers.push_back(buffer->draw_tracker);

					if (set_uniform.writable) {
						draw_trackers_usage.push_back(RDG::RESOURCE_USAGE_STORAGE_BUFFER_READ_WRITE);
					} else {
						draw_trackers_usage.push_back(RDG::RESOURCE_USAGE_STORAGE_BUFFER_READ);
					}
				} else {
					untracked_usage[buffer_id] = RDG::RESOURCE_USAGE_STORAGE_BUFFER_READ;
				}

				driver_uniform.ids.push_back(buffer->driver_id);
				_check_transfer_worker_buffer(buffer);
			} break;
			case UNIFORM_TYPE_INPUT_ATTACHMENT: {
				ERR_FAIL_COND_V_MSG(shader->is_compute, RID(), "InputAttachment (binding: " + itos(uniform.binding) + ") supplied for compute shader (this is not allowed).");

				if (uniform.get_id_count() != (uint32_t)set_uniform.length) {
					if (set_uniform.length > 1) {
						ERR_FAIL_V_MSG(RID(), "InputAttachment (binding: " + itos(uniform.binding) + ") is an array of (" + itos(set_uniform.length) + ") textures, so it should be provided equal number of texture IDs to satisfy it (IDs provided: " + itos(uniform.get_id_count()) + ").");
					} else {
						ERR_FAIL_V_MSG(RID(), "InputAttachment (binding: " + itos(uniform.binding) + ") should provide one ID referencing a texture (IDs provided: " + itos(uniform.get_id_count()) + ").");
					}
				}

				for (uint32_t j = 0; j < uniform.get_id_count(); j++) {
					RID texture_id = uniform.get_id(j);
					Texture *texture = texture_owner.get_or_null(texture_id);

					ERR_FAIL_NULL_V_MSG(texture, RID(),
							"InputAttachment (binding: " + itos(uniform.binding) + ", index " + itos(j) + ") is not a valid texture.");

					ERR_FAIL_COND_V_MSG(!(texture->usage_flags & TEXTURE_USAGE_SAMPLING_BIT), RID(),
							"InputAttachment (binding: " + itos(uniform.binding) + ", index " + itos(j) + ") needs the TEXTURE_USAGE_SAMPLING_BIT usage flag set in order to be used as uniform.");

					DEV_ASSERT(!texture->owner.is_valid() || texture_owner.get_or_null(texture->owner));

					driver_uniform.ids.push_back(texture->driver_id);
					_check_transfer_worker_texture(texture);
				}
			} break;
			default: {
			}
		}
	}

	RDD::UniformSetID driver_uniform_set = driver->uniform_set_create(driver_uniforms, shader->driver_id, p_shader_set, p_linear_pool ? frame : -1);
	ERR_FAIL_COND_V(!driver_uniform_set, RID());

	UniformSet uniform_set;
	uniform_set.driver_id = driver_uniform_set;
	uniform_set.format = shader->set_formats[p_shader_set];
	uniform_set.attachable_textures = attachable_textures;
	uniform_set.draw_trackers = draw_trackers;
	uniform_set.draw_trackers_usage = draw_trackers_usage;
	uniform_set.untracked_usage = untracked_usage;
	uniform_set.shared_textures_to_update = shared_textures_to_update;
	uniform_set.shader_set = p_shader_set;
	uniform_set.shader_id = p_shader;

	RID id = uniform_set_owner.make_rid(uniform_set);
#ifdef DEV_ENABLED
	set_resource_name(id, "RID:" + itos(id.get_id()));
#endif
	// Add dependencies.
	_add_dependency(id, p_shader);
	for (uint32_t i = 0; i < uniform_count; i++) {
		const Uniform &uniform = uniforms[i];
		int id_count = uniform.get_id_count();
		for (int j = 0; j < id_count; j++) {
			_add_dependency(id, uniform.get_id(j));
		}
	}

	return id;
}

bool RenderingDevice::uniform_set_is_valid(RID p_uniform_set) {
	_THREAD_SAFE_METHOD_

	return uniform_set_owner.owns(p_uniform_set);
}

void RenderingDevice::uniform_set_set_invalidation_callback(RID p_uniform_set, InvalidationCallback p_callback, void *p_userdata) {
	_THREAD_SAFE_METHOD_

	UniformSet *us = uniform_set_owner.get_or_null(p_uniform_set);
	ERR_FAIL_NULL(us);
	us->invalidated_callback = p_callback;
	us->invalidated_callback_userdata = p_userdata;
}

bool RenderingDevice::uniform_sets_have_linear_pools() const {
	return driver->uniform_sets_have_linear_pools();
}

/*******************/
/**** PIPELINES ****/
/*******************/

RID RenderingDevice::render_pipeline_create(RID p_shader, FramebufferFormatID p_framebuffer_format, VertexFormatID p_vertex_format, RenderPrimitive p_render_primitive, const PipelineRasterizationState &p_rasterization_state, const PipelineMultisampleState &p_multisample_state, const PipelineDepthStencilState &p_depth_stencil_state, const PipelineColorBlendState &p_blend_state, BitField<PipelineDynamicStateFlags> p_dynamic_state_flags, uint32_t p_for_render_pass, const Vector<PipelineSpecializationConstant> &p_specialization_constants) {
	// Needs a shader.
	Shader *shader = shader_owner.get_or_null(p_shader);
	ERR_FAIL_NULL_V(shader, RID());
	ERR_FAIL_COND_V_MSG(shader->is_compute, RID(), "Compute shaders can't be used in render pipelines");

	FramebufferFormat fb_format;
	{
		_THREAD_SAFE_METHOD_

		if (p_framebuffer_format == INVALID_ID) {
			// If nothing provided, use an empty one (no attachments).
			p_framebuffer_format = framebuffer_format_create(Vector<AttachmentFormat>());
		}
		ERR_FAIL_COND_V(!framebuffer_formats.has(p_framebuffer_format), RID());
		fb_format = framebuffer_formats[p_framebuffer_format];
	}

	// Validate shader vs. framebuffer.
	{
		ERR_FAIL_COND_V_MSG(p_for_render_pass >= uint32_t(fb_format.E->key().passes.size()), RID(), "Render pass requested for pipeline creation (" + itos(p_for_render_pass) + ") is out of bounds");
		const FramebufferPass &pass = fb_format.E->key().passes[p_for_render_pass];
		uint32_t output_mask = 0;
		for (int i = 0; i < pass.color_attachments.size(); i++) {
			if (pass.color_attachments[i] != ATTACHMENT_UNUSED) {
				output_mask |= 1 << i;
			}
		}
		ERR_FAIL_COND_V_MSG(shader->fragment_output_mask != output_mask, RID(),
				"Mismatch fragment shader output mask (" + itos(shader->fragment_output_mask) + ") and framebuffer color output mask (" + itos(output_mask) + ") when binding both in render pipeline.");
	}

	RDD::VertexFormatID driver_vertex_format;
	if (p_vertex_format != INVALID_ID) {
		// Uses vertices, else it does not.
		ERR_FAIL_COND_V(!vertex_formats.has(p_vertex_format), RID());
		const VertexDescriptionCache &vd = vertex_formats[p_vertex_format];
		driver_vertex_format = vertex_formats[p_vertex_format].driver_id;

		// Validate with inputs.
		for (uint32_t i = 0; i < 64; i++) {
			if (!(shader->vertex_input_mask & ((uint64_t)1) << i)) {
				continue;
			}
			bool found = false;
			for (int j = 0; j < vd.vertex_formats.size(); j++) {
				if (vd.vertex_formats[j].location == i) {
					found = true;
					break;
				}
			}

			ERR_FAIL_COND_V_MSG(!found, RID(),
					"Shader vertex input location (" + itos(i) + ") not provided in vertex input description for pipeline creation.");
		}

	} else {
		ERR_FAIL_COND_V_MSG(shader->vertex_input_mask != 0, RID(),
				"Shader contains vertex inputs, but no vertex input description was provided for pipeline creation.");
	}

	ERR_FAIL_INDEX_V(p_render_primitive, RENDER_PRIMITIVE_MAX, RID());

	ERR_FAIL_INDEX_V(p_rasterization_state.cull_mode, 3, RID());

	if (p_multisample_state.sample_mask.size()) {
		// Use sample mask.
		ERR_FAIL_COND_V((int)TEXTURE_SAMPLES_COUNT[p_multisample_state.sample_count] != p_multisample_state.sample_mask.size(), RID());
	}

	ERR_FAIL_INDEX_V(p_depth_stencil_state.depth_compare_operator, COMPARE_OP_MAX, RID());

	ERR_FAIL_INDEX_V(p_depth_stencil_state.front_op.fail, STENCIL_OP_MAX, RID());
	ERR_FAIL_INDEX_V(p_depth_stencil_state.front_op.pass, STENCIL_OP_MAX, RID());
	ERR_FAIL_INDEX_V(p_depth_stencil_state.front_op.depth_fail, STENCIL_OP_MAX, RID());
	ERR_FAIL_INDEX_V(p_depth_stencil_state.front_op.compare, COMPARE_OP_MAX, RID());

	ERR_FAIL_INDEX_V(p_depth_stencil_state.back_op.fail, STENCIL_OP_MAX, RID());
	ERR_FAIL_INDEX_V(p_depth_stencil_state.back_op.pass, STENCIL_OP_MAX, RID());
	ERR_FAIL_INDEX_V(p_depth_stencil_state.back_op.depth_fail, STENCIL_OP_MAX, RID());
	ERR_FAIL_INDEX_V(p_depth_stencil_state.back_op.compare, COMPARE_OP_MAX, RID());

	ERR_FAIL_INDEX_V(p_blend_state.logic_op, LOGIC_OP_MAX, RID());

	const FramebufferPass &pass = fb_format.E->key().passes[p_for_render_pass];
	ERR_FAIL_COND_V(p_blend_state.attachments.size() < pass.color_attachments.size(), RID());
	for (int i = 0; i < pass.color_attachments.size(); i++) {
		if (pass.color_attachments[i] != ATTACHMENT_UNUSED) {
			ERR_FAIL_INDEX_V(p_blend_state.attachments[i].src_color_blend_factor, BLEND_FACTOR_MAX, RID());
			ERR_FAIL_INDEX_V(p_blend_state.attachments[i].dst_color_blend_factor, BLEND_FACTOR_MAX, RID());
			ERR_FAIL_INDEX_V(p_blend_state.attachments[i].color_blend_op, BLEND_OP_MAX, RID());

			ERR_FAIL_INDEX_V(p_blend_state.attachments[i].src_alpha_blend_factor, BLEND_FACTOR_MAX, RID());
			ERR_FAIL_INDEX_V(p_blend_state.attachments[i].dst_alpha_blend_factor, BLEND_FACTOR_MAX, RID());
			ERR_FAIL_INDEX_V(p_blend_state.attachments[i].alpha_blend_op, BLEND_OP_MAX, RID());
		}
	}

	for (int i = 0; i < shader->specialization_constants.size(); i++) {
		const ShaderSpecializationConstant &sc = shader->specialization_constants[i];
		for (int j = 0; j < p_specialization_constants.size(); j++) {
			const PipelineSpecializationConstant &psc = p_specialization_constants[j];
			if (psc.constant_id == sc.constant_id) {
				ERR_FAIL_COND_V_MSG(psc.type != sc.type, RID(), "Specialization constant provided for id (" + itos(sc.constant_id) + ") is of the wrong type.");
				break;
			}
		}
	}

	RenderPipeline pipeline;
	pipeline.driver_id = driver->render_pipeline_create(
			shader->driver_id,
			driver_vertex_format,
			p_render_primitive,
			p_rasterization_state,
			p_multisample_state,
			p_depth_stencil_state,
			p_blend_state,
			pass.color_attachments,
			p_dynamic_state_flags,
			fb_format.render_pass,
			p_for_render_pass,
			p_specialization_constants);
	ERR_FAIL_COND_V(!pipeline.driver_id, RID());

	if (pipeline_cache_enabled) {
		_update_pipeline_cache();
	}

	pipeline.shader = p_shader;
	pipeline.shader_driver_id = shader->driver_id;
	pipeline.shader_layout_hash = shader->layout_hash;
	pipeline.set_formats = shader->set_formats;
	pipeline.push_constant_size = shader->push_constant_size;
	pipeline.stage_bits = shader->stage_bits;

#ifdef DEBUG_ENABLED
	pipeline.validation.dynamic_state = p_dynamic_state_flags;
	pipeline.validation.framebuffer_format = p_framebuffer_format;
	pipeline.validation.render_pass = p_for_render_pass;
	pipeline.validation.vertex_format = p_vertex_format;
	pipeline.validation.uses_restart_indices = p_render_primitive == RENDER_PRIMITIVE_TRIANGLE_STRIPS_WITH_RESTART_INDEX;

	static const uint32_t primitive_divisor[RENDER_PRIMITIVE_MAX] = {
		1, 2, 1, 1, 1, 3, 1, 1, 1, 1, 1
	};
	pipeline.validation.primitive_divisor = primitive_divisor[p_render_primitive];
	static const uint32_t primitive_minimum[RENDER_PRIMITIVE_MAX] = {
		1,
		2,
		2,
		2,
		2,
		3,
		3,
		3,
		3,
		3,
		1,
	};
	pipeline.validation.primitive_minimum = primitive_minimum[p_render_primitive];
#endif

	// Create ID to associate with this pipeline.
	RID id = render_pipeline_owner.make_rid(pipeline);
	{
		_THREAD_SAFE_METHOD_

#ifdef DEV_ENABLED
		set_resource_name(id, "RID:" + itos(id.get_id()));
#endif
		// Now add all the dependencies.
		_add_dependency(id, p_shader);
	}

	return id;
}

bool RenderingDevice::render_pipeline_is_valid(RID p_pipeline) {
	_THREAD_SAFE_METHOD_

	return render_pipeline_owner.owns(p_pipeline);
}

RID RenderingDevice::compute_pipeline_create(RID p_shader, const Vector<PipelineSpecializationConstant> &p_specialization_constants) {
	Shader *shader;

	{
		_THREAD_SAFE_METHOD_

		// Needs a shader.
		shader = shader_owner.get_or_null(p_shader);
		ERR_FAIL_NULL_V(shader, RID());

		ERR_FAIL_COND_V_MSG(!shader->is_compute, RID(),
				"Non-compute shaders can't be used in compute pipelines");
	}

	for (int i = 0; i < shader->specialization_constants.size(); i++) {
		const ShaderSpecializationConstant &sc = shader->specialization_constants[i];
		for (int j = 0; j < p_specialization_constants.size(); j++) {
			const PipelineSpecializationConstant &psc = p_specialization_constants[j];
			if (psc.constant_id == sc.constant_id) {
				ERR_FAIL_COND_V_MSG(psc.type != sc.type, RID(), "Specialization constant provided for id (" + itos(sc.constant_id) + ") is of the wrong type.");
				break;
			}
		}
	}

	ComputePipeline pipeline;
	pipeline.driver_id = driver->compute_pipeline_create(shader->driver_id, p_specialization_constants);
	ERR_FAIL_COND_V(!pipeline.driver_id, RID());

	if (pipeline_cache_enabled) {
		_update_pipeline_cache();
	}

	pipeline.shader = p_shader;
	pipeline.shader_driver_id = shader->driver_id;
	pipeline.shader_layout_hash = shader->layout_hash;
	pipeline.set_formats = shader->set_formats;
	pipeline.push_constant_size = shader->push_constant_size;
	pipeline.local_group_size[0] = shader->compute_local_size[0];
	pipeline.local_group_size[1] = shader->compute_local_size[1];
	pipeline.local_group_size[2] = shader->compute_local_size[2];

	// Create ID to associate with this pipeline.
	RID id = compute_pipeline_owner.make_rid(pipeline);
	{
		_THREAD_SAFE_METHOD_

#ifdef DEV_ENABLED
		set_resource_name(id, "RID:" + itos(id.get_id()));
#endif
		// Now add all the dependencies.
		_add_dependency(id, p_shader);
	}

	return id;
}

bool RenderingDevice::compute_pipeline_is_valid(RID p_pipeline) {
	_THREAD_SAFE_METHOD_

	return compute_pipeline_owner.owns(p_pipeline);
}

/****************/
/**** SCREEN ****/
/****************/

uint32_t RenderingDevice::_get_swap_chain_desired_count() const {
	return MAX(2U, uint32_t(GLOBAL_GET("rendering/rendering_device/vsync/swapchain_image_count")));
}

Error RenderingDevice::screen_create(DisplayServer::WindowID p_screen) {
	_THREAD_SAFE_METHOD_

	RenderingContextDriver::SurfaceID surface = context->surface_get_from_window(p_screen);
	ERR_FAIL_COND_V_MSG(surface == 0, ERR_CANT_CREATE, "A surface was not created for the screen.");

	HashMap<DisplayServer::WindowID, RDD::SwapChainID>::ConstIterator it = screen_swap_chains.find(p_screen);
	ERR_FAIL_COND_V_MSG(it != screen_swap_chains.end(), ERR_CANT_CREATE, "A swap chain was already created for the screen.");

	RDD::SwapChainID swap_chain = driver->swap_chain_create(surface);
	ERR_FAIL_COND_V_MSG(swap_chain.id == 0, ERR_CANT_CREATE, "Unable to create swap chain.");

	screen_swap_chains[p_screen] = swap_chain;

	return OK;
}

/// 为整个屏幕的绘制准备好可用的framebuffer，并处理与交换链有关的一系列细节，包括
/// - presentation、acquire、resize和失败恢复
/// 它的职责是在渲染前保证屏幕绘制环境正确可用
Error RenderingDevice::screen_prepare_for_drawing(DisplayServer::WindowID p_screen) {
	_THREAD_SAFE_METHOD_

	// After submitting work, acquire the swapchain image(s).
	// 在提交工作后，获取交换链图像。校验交换链的存在性
	HashMap<DisplayServer::WindowID, RDD::SwapChainID>::ConstIterator it = screen_swap_chains.find(p_screen);
	ERR_FAIL_COND_V_MSG(it == screen_swap_chains.end(), ERR_CANT_CREATE, "A swap chain was not created for the screen.");

	// Erase the framebuffer corresponding to this screen from the map in case any of the operations fail.
	// 把这个屏幕上上次缓存的framebuffer移除，避免使用到无效资源。
	screen_framebuffers.erase(p_screen);

	// If this frame has already queued this swap chain for presentation, we present it and remove it from the pending list.
	/// 如果本帧已经请求过呈现了，我们就呈现它，并且从等待队列中删除它。
	uint32_t to_present_index = 0;
	while (to_present_index < frames[frame].swap_chains_to_present.size()) {
		if (frames[frame].swap_chains_to_present[to_present_index] == it->value) {
			// 直接执行队列并呈现。
			driver->command_queue_execute_and_present(present_queue, {}, {}, {}, {}, it->value);
			frames[frame].swap_chains_to_present.remove_at(to_present_index);
		} else {
			to_present_index++;
		}
	}

	bool resize_required = false;
	// 从交换链获取一个framebuffer
	RDD::FramebufferID framebuffer = driver->swap_chain_acquire_framebuffer(main_queue, it->value, resize_required);
	if (resize_required) {
		// Flush everything so nothing can be using the swap chain before resizing it.
		// 调用flush确保没有任何命令在使用交换链，然后再进行resize（直接提交刷新，并且阻塞等待）
		_flush_and_stall_for_all_frames();

		// 改变交换链的大小
		Error err = driver->swap_chain_resize(main_queue, it->value, _get_swap_chain_desired_count());
		if (err != OK) {
			// Resize is allowed to fail silently because the window can be minimized.
			return err;
		}

		// 然后重新获取framebuffer
		framebuffer = driver->swap_chain_acquire_framebuffer(main_queue, it->value, resize_required);
	}

	// framebuffer可能无效
	if (framebuffer.id == 0) {
		// Some drivers like NVIDIA are fast enough to invalidate the swap chain between resizing and acquisition (GH-94104).
		// This typically occurs during continuous window resizing operations, especially if done quickly.
		// Allow this to fail silently since it has no visual consequences.
		// 有些显卡驱动（特别是 NVIDIA 的）在窗口 调整大小 (resize) 时非常激进，
		// 可能会在 交换链 resize 和获取 framebuffer (acquire) 这两个操作之间，
		// 就让交换链失效（invalidate）。
		// 这种情况常见于 用户不停地快速拖动窗口边框调整大小。这种失败 不会造成画面异常：
		// - 窗口在被快速 resize 时，本来也没有稳定的画面能显示；
		// - 即使获取 framebuffer 失败，本帧也就不画，画面会在 resize 稳定后自动恢复。
		return ERR_CANT_CREATE;
	}

	// Store the framebuffer that will be used next to draw to this screen.
	// 将framebuffer保存到screen_framebuffers中，作为后续渲染目标
	screen_framebuffers[p_screen] = framebuffer;
	// 把交换链加回到待呈现列表中，表示本帧结束时要呈现它
	frames[frame].swap_chains_to_present.push_back(it->value);

	return OK;
}

int RenderingDevice::screen_get_width(DisplayServer::WindowID p_screen) const {
	_THREAD_SAFE_METHOD_

	RenderingContextDriver::SurfaceID surface = context->surface_get_from_window(p_screen);
	ERR_FAIL_COND_V_MSG(surface == 0, 0, "A surface was not created for the screen.");
	return context->surface_get_width(surface);
}

int RenderingDevice::screen_get_height(DisplayServer::WindowID p_screen) const {
	_THREAD_SAFE_METHOD_

	RenderingContextDriver::SurfaceID surface = context->surface_get_from_window(p_screen);
	ERR_FAIL_COND_V_MSG(surface == 0, 0, "A surface was not created for the screen.");
	return context->surface_get_height(surface);
}

int RenderingDevice::screen_get_pre_rotation_degrees(DisplayServer::WindowID p_screen) const {
	_THREAD_SAFE_METHOD_

	HashMap<DisplayServer::WindowID, RDD::SwapChainID>::ConstIterator it = screen_swap_chains.find(p_screen);
	ERR_FAIL_COND_V_MSG(it == screen_swap_chains.end(), ERR_CANT_CREATE, "A swap chain was not created for the screen.");

	return driver->swap_chain_get_pre_rotation_degrees(it->value);
}

RenderingDevice::FramebufferFormatID RenderingDevice::screen_get_framebuffer_format(DisplayServer::WindowID p_screen) const {
	_THREAD_SAFE_METHOD_

	HashMap<DisplayServer::WindowID, RDD::SwapChainID>::ConstIterator it = screen_swap_chains.find(p_screen);
	ERR_FAIL_COND_V_MSG(it == screen_swap_chains.end(), FAILED, "Screen was never prepared.");

	DataFormat format = driver->swap_chain_get_format(it->value);
	ERR_FAIL_COND_V(format == DATA_FORMAT_MAX, INVALID_ID);

	AttachmentFormat attachment;
	attachment.format = format;
	attachment.samples = TEXTURE_SAMPLES_1;
	attachment.usage_flags = TEXTURE_USAGE_COLOR_ATTACHMENT_BIT;
	Vector<AttachmentFormat> screen_attachment;
	screen_attachment.push_back(attachment);
	return const_cast<RenderingDevice *>(this)->framebuffer_format_create(screen_attachment);
}

Error RenderingDevice::screen_free(DisplayServer::WindowID p_screen) {
	_THREAD_SAFE_METHOD_

	HashMap<DisplayServer::WindowID, RDD::SwapChainID>::ConstIterator it = screen_swap_chains.find(p_screen);
	ERR_FAIL_COND_V_MSG(it == screen_swap_chains.end(), FAILED, "Screen was never created.");

	// Flush everything so nothing can be using the swap chain before erasing it.
	_flush_and_stall_for_all_frames();

	const DisplayServer::WindowID screen = it->key;
	const RDD::SwapChainID swap_chain = it->value;
	driver->swap_chain_free(swap_chain);
	screen_framebuffers.erase(screen);
	screen_swap_chains.erase(screen);

	return OK;
}

/*******************/
/**** DRAW LIST ****/
/*******************/

RenderingDevice::DrawListID RenderingDevice::draw_list_begin_for_screen(DisplayServer::WindowID p_screen, const Color &p_clear_color) {
	ERR_RENDER_THREAD_GUARD_V(INVALID_ID);

	ERR_FAIL_COND_V_MSG(draw_list != nullptr, INVALID_ID, "Only one draw list can be active at the same time.");
	ERR_FAIL_COND_V_MSG(compute_list != nullptr, INVALID_ID, "Only one draw/compute list can be active at the same time.");

	RenderingContextDriver::SurfaceID surface = context->surface_get_from_window(p_screen);
	HashMap<DisplayServer::WindowID, RDD::SwapChainID>::ConstIterator sc_it = screen_swap_chains.find(p_screen);
	HashMap<DisplayServer::WindowID, RDD::FramebufferID>::ConstIterator fb_it = screen_framebuffers.find(p_screen);
	ERR_FAIL_COND_V_MSG(surface == 0, 0, "A surface was not created for the screen.");
	ERR_FAIL_COND_V_MSG(sc_it == screen_swap_chains.end(), INVALID_ID, "Screen was never prepared.");
	ERR_FAIL_COND_V_MSG(fb_it == screen_framebuffers.end(), INVALID_ID, "Framebuffer was never prepared.");

	Rect2i viewport = Rect2i(0, 0, context->surface_get_width(surface), context->surface_get_height(surface));

	_draw_list_allocate(viewport, 0);
#ifdef DEBUG_ENABLED
	draw_list_framebuffer_format = screen_get_framebuffer_format(p_screen);
#endif
	draw_list_subpass_count = 1;

	RDD::RenderPassClearValue clear_value;
	clear_value.color = p_clear_color;

	RDD::RenderPassID render_pass = driver->swap_chain_get_render_pass(sc_it->value);
	draw_graph.add_draw_list_begin(render_pass, fb_it->value, viewport, RDG::ATTACHMENT_OPERATION_CLEAR, clear_value, true, false, RDD::BreadcrumbMarker::BLIT_PASS, split_swapchain_into_its_own_cmd_buffer);

	draw_graph.add_draw_list_set_viewport(viewport);
	draw_graph.add_draw_list_set_scissor(viewport);

	return int64_t(ID_TYPE_DRAW_LIST) << ID_BASE_SHIFT;
}


// 启动一个新的Draw List（一次渲染列表/记录序列），返回一个ID
// 参数：
// - p_framebuffer：目标帧缓冲 RID（包含颜色/深度贴图）
// - p_draw_flags：控制是否清屏/忽略某附件等的标志位（按附件编号位移）
// - p_clear_color_values：对应各个颜色附件的清除颜色列表
// - p_clear_depth_value / p_clear_stencil_value：清除深度/模板用的值
// - p_region：在帧缓冲内使用的自定义矩形区域（视口/剪裁），可以只对这个区域进行操作
// - p_breadcrumb：面包屑/诊断标记，用于调试跟踪
RenderingDevice::DrawListID RenderingDevice::draw_list_begin(RID p_framebuffer,
	BitField<DrawFlags> p_draw_flags,
	const Vector<Color> &p_clear_color_values,
	float p_clear_depth_value,
	uint32_t p_clear_stencil_value,
	const Rect2 &p_region,
	uint32_t p_breadcrumb)
{
	// 确保在渲染线程中调用
	ERR_RENDER_THREAD_GUARD_V(INVALID_ID);

	// 防止嵌套/并发：一次只能有一个活动的 draw_list。
	ERR_FAIL_COND_V_MSG(draw_list != nullptr, INVALID_ID, "Only one draw list can be active at the same time.");

	// 通过 RID 取帧缓冲对象。
	Framebuffer *framebuffer = framebuffer_owner.get_or_null(p_framebuffer);
	ERR_FAIL_NULL_V(framebuffer, INVALID_ID);

	// 视口左上角偏移（默认 0,0）。
	Point2i viewport_offset;
	// 缺省使用整个帧缓冲尺寸。
	Point2i viewport_size = framebuffer->size;

	// 如果提供了自定义区域，且不等于默认或整幅尺寸
	if (p_region != Rect2() && p_region != Rect2(Vector2(), viewport_size)) { // Check custom region.
		Rect2i viewport(viewport_offset, viewport_size);// 当前允许的可用矩形（整幅）。
		Rect2i regioni = p_region;// 将浮点 Rect2 转为整型 Rect2i（像素对齐）。
		if (!((regioni.position.x >= viewport.position.x) && (regioni.position.y >= viewport.position.y) &&
					((regioni.position.x + regioni.size.x) <= (viewport.position.x + viewport.size.x)) &&
					((regioni.position.y + regioni.size.y) <= (viewport.position.y + viewport.size.y)))) {
			// 自定义区域必须完全落在帧缓冲内。
			ERR_FAIL_V_MSG(INVALID_ID, "When supplying a custom region, it must be contained within the framebuffer rectangle");
		}

		viewport_offset = regioni.position;// 使用自定义区域的起点作为视口偏移。
		viewport_size = regioni.size;// 使用自定义区域的尺寸作为视口大小。
	}

	// 每个附件（颜色/深度）对应的操作：默认/清除/忽略（thread_local 复用内存，减少分配）。
	thread_local LocalVector<RDG::AttachmentOperation> operations;
	// 各附件的清除值（颜色或深度/模板）。
	thread_local LocalVector<RDD::RenderPassClearValue> clear_values;
	// 资源跟踪器（用于依赖/屏障/并发写读分析）。
	thread_local LocalVector<RDG::ResourceTracker *> resource_trackers;
	// 每个被用到的资源的用途（颜色读写/深度读写）。
	thread_local LocalVector<RDG::ResourceUsage> resource_usages;
	bool uses_color = false;	// 是否涉及颜色附件（优化/快速路径标志）
	bool uses_depth = false;	// 是否涉及深度/模板附件
	operations.resize(framebuffer->texture_ids.size());	// 操作数组刷新
	clear_values.resize(framebuffer->texture_ids.size());	// 清除值数组刷新
	resource_trackers.clear();	// 资源跟踪器清空
	resource_usages.clear();	// 资源用途清空

	// 当前处理的是第几个“颜色附件”，与清屏颜色数组一一对应
	uint32_t color_index = 0;
	// 遍历帧缓冲的所有纹理
	for (int i = 0; i < framebuffer->texture_ids.size(); i++) {
		// 获取纹理的资源ID
		RID texture_rid = framebuffer->texture_ids[i];
		// 获取到实际的纹理对象
		Texture *texture = texture_owner.get_or_null(texture_rid);
		// 如果纹理为空，那么这个纹理的操作就设置为默认，清除值为空
		if (texture == nullptr) {
			operations[i] = RDG::ATTACHMENT_OPERATION_DEFAULT;
			clear_values[i] = RDD::RenderPassClearValue();
			continue;
		}

		// Indicate the texture will get modified for the shared texture fallback.
		// 指明该纹理会被修改，用于“共享纹理回退”路径（例如多设备/后台共享时需要特殊处理/复制）
		_texture_update_shared_fallback(texture_rid, texture, true);

		// 默认操作和清除值
		RDG::AttachmentOperation operation = RDG::ATTACHMENT_OPERATION_DEFAULT;
		RDD::RenderPassClearValue clear_value;
		// 如果是颜色附件
		if (texture->usage_flags & TEXTURE_USAGE_COLOR_ATTACHMENT_BIT) {
			if (p_draw_flags.has_flag(DrawFlags(DRAW_CLEAR_COLOR_0 << color_index))) {
			// 看标记中是否有清除该颜色附件的标志。

				// 需清但未提供清除色则报错。
				ERR_FAIL_COND_V_MSG(color_index >= p_clear_color_values.size(), INVALID_ID, vformat("Color texture (%d) was specified to be cleared but no color value was provided.", color_index));
				// 记录操作为清除
				operation = RDG::ATTACHMENT_OPERATION_CLEAR;
				// 设置对应清除颜色
				clear_value.color = p_clear_color_values[color_index];
			} else if (p_draw_flags.has_flag(DrawFlags(DRAW_IGNORE_COLOR_0 << color_index))) {
			// 要求忽略该颜色附件

				// 不作为本次渲染的有效附件（不读不写）。
				operation = RDG::ATTACHMENT_OPERATION_IGNORE;
			}

			// 把该纹理的绘制跟踪器登记到本次 draw list。
			resource_trackers.push_back(texture->draw_tracker);
			// 标记用途为颜色附件读写（用于依赖/屏障）
			resource_usages.push_back(RDG::RESOURCE_USAGE_ATTACHMENT_COLOR_READ_WRITE);
			uses_color = true;	// 有颜色附件参与。
			color_index++;	// 推进颜色附件的索引（注意与附件 i 不一定相同，因为深度不计入 color_index）。
		}
		else if (texture->usage_flags & TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
		// 这是深度/模板附件

			// 是否需要清除深度或模板
			if (p_draw_flags.has_flag(DRAW_CLEAR_DEPTH) || p_draw_flags.has_flag(DRAW_CLEAR_STENCIL)) {
				// 只要有一个就清除深度/模板两个的值
				operation = RDG::ATTACHMENT_OPERATION_CLEAR;
				clear_value.depth = p_clear_depth_value;
				clear_value.stencil = p_clear_stencil_value;
			}
			// 要求忽略深度或模板
			else if (p_draw_flags.has_flag(DRAW_IGNORE_DEPTH) || p_draw_flags.has_flag(DRAW_IGNORE_STENCIL)) {
				operation = RDG::ATTACHMENT_OPERATION_IGNORE;	// 本次渲染不使用它
			}

			// 等级深度/模板附件的资源跟踪器
			resource_trackers.push_back(texture->draw_tracker);
			// 标记用途为深度/模板读写
			resource_usages.push_back(RDG::RESOURCE_USAGE_ATTACHMENT_DEPTH_STENCIL_READ_WRITE);
			uses_depth = true;	// 深度/模板参与
		}

		// 记录第i个附件的操作和清除值
		operations[i] = operation;
		clear_values[i] = clear_value;
	}

	// 通知渲染图本次draw list的开始，其中包括：
	//		- 帧缓冲对象
	//		- 视口位置和大小
	//		- 每个附件的操作和清除值
	//		- 是否使用颜色/深度附件（优化标志）
	//		- 调试面包屑。
	draw_graph.add_draw_list_begin(framebuffer->framebuffer_cache, Rect2i(viewport_offset, viewport_size), operations, clear_values, uses_color, uses_depth, p_breadcrumb);
	// 把资源用途/跟踪信息也提交给渲染图（用于依赖分析和插入屏障）。
	draw_graph.add_draw_list_usages(resource_trackers, resource_usages);

	// Mark textures as bound.
	// 记录本次draw list绑定过的纹理（用于结束时回收/解绑/状态维护）
	draw_list_bound_textures.clear();	// 先清空记录

	// 遍历帧缓冲的所有附件
	for (int i = 0; i < framebuffer->texture_ids.size(); i++) {
		// 取纹理对象
		Texture *texture = texture_owner.get_or_null(framebuffer->texture_ids[i]);
		if (texture == nullptr) {
			continue;
		}

		texture->bound = true;	// 标记为已绑定
		draw_list_bound_textures.push_back(framebuffer->texture_ids[i]);	// 加入绑定列表
	}

	// 分配一个drawlist的内存空间，并且将视口信息设置给drawlist
	_draw_list_allocate(Rect2i(viewport_offset, viewport_size), 0);
#ifdef DEBUG_ENABLED
	// Debug：记录下当前帧缓冲的“格式ID”，用于后续一致性校验。
	draw_list_framebuffer_format = framebuffer->format_id;
#endif
	// 当前子通道从0开始
	draw_list_current_subpass = 0;

	// 通过 format_id 查找帧缓冲格式的关键结构（包含各 subpass 定义）。
	const FramebufferFormatKey &key = framebuffer_formats[framebuffer->format_id].E->key();
	// 记录总的 subpass 数量（后续切换管线/子通道时用）。
	draw_list_subpass_count = key.passes.size();

	// 构造最终的视口矩形
	Rect2i viewport_rect(viewport_offset, viewport_size);
	// 将视口信息记录到渲染图（后端真正执行时应用）
	draw_graph.add_draw_list_set_viewport(viewport_rect);
	// 同步设置裁剪区域=视口矩形（确保只在该区域内绘制）
	draw_graph.add_draw_list_set_scissor(viewport_rect);

	// 返回一个编码后的 DrawListID（用类型左移形成句柄）。
	return int64_t(ID_TYPE_DRAW_LIST) << ID_BASE_SHIFT;
}

#ifndef DISABLE_DEPRECATED
Error RenderingDevice::draw_list_begin_split(RID p_framebuffer, uint32_t p_splits, DrawListID *r_split_ids, InitialAction p_initial_color_action, FinalAction p_final_color_action, InitialAction p_initial_depth_action, FinalAction p_final_depth_action, const Vector<Color> &p_clear_color_values, float p_clear_depth, uint32_t p_clear_stencil, const Rect2 &p_region, const Vector<RID> &p_storage_textures) {
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "Deprecated. Split draw lists are used automatically by RenderingDevice.");
}
#endif

RenderingDevice::DrawList *RenderingDevice::_get_draw_list_ptr(DrawListID p_id) {
	if (p_id < 0) {
		return nullptr;
	}

	if (!draw_list) {
		return nullptr;
	} else if (p_id == (int64_t(ID_TYPE_DRAW_LIST) << ID_BASE_SHIFT)) {
		return draw_list;
	} else {
		return nullptr;
	}
}

void RenderingDevice::draw_list_set_blend_constants(DrawListID p_list, const Color &p_color) {
	ERR_RENDER_THREAD_GUARD();

	DrawList *dl = _get_draw_list_ptr(p_list);
	ERR_FAIL_NULL(dl);
#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(!dl->validation.active, "Submitted Draw Lists can no longer be modified.");
#endif

	draw_graph.add_draw_list_set_blend_constants(p_color);
}

void RenderingDevice::draw_list_bind_render_pipeline(DrawListID p_list, RID p_render_pipeline) {
	ERR_RENDER_THREAD_GUARD();

	DrawList *dl = _get_draw_list_ptr(p_list);
	ERR_FAIL_NULL(dl);
#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(!dl->validation.active, "Submitted Draw Lists can no longer be modified.");
#endif

	const RenderPipeline *pipeline = render_pipeline_owner.get_or_null(p_render_pipeline);
	ERR_FAIL_NULL(pipeline);
#ifdef DEBUG_ENABLED
	ERR_FAIL_COND(pipeline->validation.framebuffer_format != draw_list_framebuffer_format && pipeline->validation.render_pass != draw_list_current_subpass);
#endif

	if (p_render_pipeline == dl->state.pipeline) {
		return; // Redundant state, return.
	}

	dl->state.pipeline = p_render_pipeline;

	draw_graph.add_draw_list_bind_pipeline(pipeline->driver_id, pipeline->stage_bits);

	if (dl->state.pipeline_shader != pipeline->shader) {
		// Shader changed, so descriptor sets may become incompatible.

		uint32_t pcount = pipeline->set_formats.size(); // Formats count in this pipeline.
		dl->state.set_count = MAX(dl->state.set_count, pcount);
		const uint32_t *pformats = pipeline->set_formats.ptr(); // Pipeline set formats.

		uint32_t first_invalid_set = UINT32_MAX; // All valid by default.
		if (pipeline->push_constant_size != dl->state.pipeline_push_constant_size) {
			// All sets must be invalidated as the pipeline layout is not compatible if the push constant range is different.
			dl->state.pipeline_push_constant_size = pipeline->push_constant_size;
			first_invalid_set = 0;
		} else {
			switch (driver->api_trait_get(RDD::API_TRAIT_SHADER_CHANGE_INVALIDATION)) {
				case RDD::SHADER_CHANGE_INVALIDATION_ALL_BOUND_UNIFORM_SETS: {
					first_invalid_set = 0;
				} break;
				case RDD::SHADER_CHANGE_INVALIDATION_INCOMPATIBLE_SETS_PLUS_CASCADE: {
					for (uint32_t i = 0; i < pcount; i++) {
						if (dl->state.sets[i].pipeline_expected_format != pformats[i]) {
							first_invalid_set = i;
							break;
						}
					}
				} break;
				case RDD::SHADER_CHANGE_INVALIDATION_ALL_OR_NONE_ACCORDING_TO_LAYOUT_HASH: {
					if (dl->state.pipeline_shader_layout_hash != pipeline->shader_layout_hash) {
						first_invalid_set = 0;
					}
				} break;
			}
		}

		if (pipeline->push_constant_size) {
#ifdef DEBUG_ENABLED
			dl->validation.pipeline_push_constant_supplied = false;
#endif
		}

		for (uint32_t i = 0; i < pcount; i++) {
			dl->state.sets[i].bound = dl->state.sets[i].bound && i < first_invalid_set;
			dl->state.sets[i].pipeline_expected_format = pformats[i];
		}

		for (uint32_t i = pcount; i < dl->state.set_count; i++) {
			// Unbind the ones above (not used) if exist.
			dl->state.sets[i].bound = false;
		}

		dl->state.set_count = pcount; // Update set count.

		dl->state.pipeline_shader = pipeline->shader;
		dl->state.pipeline_shader_driver_id = pipeline->shader_driver_id;
		dl->state.pipeline_shader_layout_hash = pipeline->shader_layout_hash;
	}

#ifdef DEBUG_ENABLED
	// Update render pass pipeline info.
	dl->validation.pipeline_active = true;
	dl->validation.pipeline_dynamic_state = pipeline->validation.dynamic_state;
	dl->validation.pipeline_vertex_format = pipeline->validation.vertex_format;
	dl->validation.pipeline_uses_restart_indices = pipeline->validation.uses_restart_indices;
	dl->validation.pipeline_primitive_divisor = pipeline->validation.primitive_divisor;
	dl->validation.pipeline_primitive_minimum = pipeline->validation.primitive_minimum;
	dl->validation.pipeline_push_constant_size = pipeline->push_constant_size;
#endif
}

void RenderingDevice::draw_list_bind_uniform_set(DrawListID p_list, RID p_uniform_set, uint32_t p_index) {
	ERR_RENDER_THREAD_GUARD();

#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(p_index >= driver->limit_get(LIMIT_MAX_BOUND_UNIFORM_SETS) || p_index >= MAX_UNIFORM_SETS,
			"Attempting to bind a descriptor set (" + itos(p_index) + ") greater than what the hardware supports (" + itos(driver->limit_get(LIMIT_MAX_BOUND_UNIFORM_SETS)) + ").");
#endif
	DrawList *dl = _get_draw_list_ptr(p_list);
	ERR_FAIL_NULL(dl);

#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(!dl->validation.active, "Submitted Draw Lists can no longer be modified.");
#endif

	const UniformSet *uniform_set = uniform_set_owner.get_or_null(p_uniform_set);
	ERR_FAIL_NULL(uniform_set);

	if (p_index > dl->state.set_count) {
		dl->state.set_count = p_index;
	}

	dl->state.sets[p_index].uniform_set_driver_id = uniform_set->driver_id; // Update set pointer.
	dl->state.sets[p_index].bound = false; // Needs rebind.
	dl->state.sets[p_index].uniform_set_format = uniform_set->format;
	dl->state.sets[p_index].uniform_set = p_uniform_set;

#ifdef DEBUG_ENABLED
	{ // Validate that textures bound are not attached as framebuffer bindings.
		uint32_t attachable_count = uniform_set->attachable_textures.size();
		const UniformSet::AttachableTexture *attachable_ptr = uniform_set->attachable_textures.ptr();
		uint32_t bound_count = draw_list_bound_textures.size();
		const RID *bound_ptr = draw_list_bound_textures.ptr();
		for (uint32_t i = 0; i < attachable_count; i++) {
			for (uint32_t j = 0; j < bound_count; j++) {
				ERR_FAIL_COND_MSG(attachable_ptr[i].texture == bound_ptr[j],
						"Attempted to use the same texture in framebuffer attachment and a uniform (set: " + itos(p_index) + ", binding: " + itos(attachable_ptr[i].bind) + "), this is not allowed.");
			}
		}
	}
#endif
}

void RenderingDevice::draw_list_bind_vertex_array(DrawListID p_list, RID p_vertex_array) {
	ERR_RENDER_THREAD_GUARD();

	// 从绘制列表ID获取真正的绘制列表结构
	DrawList *dl = _get_draw_list_ptr(p_list);
	ERR_FAIL_NULL(dl);
#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(!dl->validation.active, "Submitted Draw Lists can no longer be modified.");
#endif

	// 获取真正的顶点数组
	VertexArray *vertex_array = vertex_array_owner.get_or_null(p_vertex_array);
	ERR_FAIL_NULL(vertex_array);

	if (dl->state.vertex_array == p_vertex_array) {
		return; // Already set.
	}

	_check_transfer_worker_vertex_array(vertex_array);

	// 绘制列表状态的顶点数组拿到手
	dl->state.vertex_array = p_vertex_array;

#ifdef DEBUG_ENABLED
	dl->validation.vertex_format = vertex_array->description;
	dl->validation.vertex_max_instances_allowed = vertex_array->max_instances_allowed;
#endif
	dl->validation.vertex_array_size = vertex_array->vertex_count;

	draw_graph.add_draw_list_bind_vertex_buffers(vertex_array->buffers, vertex_array->offsets);

	for (int i = 0; i < vertex_array->draw_trackers.size(); i++) {
		draw_graph.add_draw_list_usage(vertex_array->draw_trackers[i], RDG::RESOURCE_USAGE_VERTEX_BUFFER_READ);
	}
}

void RenderingDevice::draw_list_bind_index_array(DrawListID p_list, RID p_index_array) {
	ERR_RENDER_THREAD_GUARD();

	DrawList *dl = _get_draw_list_ptr(p_list);
	ERR_FAIL_NULL(dl);
#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(!dl->validation.active, "Submitted Draw Lists can no longer be modified.");
#endif

	IndexArray *index_array = index_array_owner.get_or_null(p_index_array);
	ERR_FAIL_NULL(index_array);

	if (dl->state.index_array == p_index_array) {
		return; // Already set.
	}

	_check_transfer_worker_index_array(index_array);

	dl->state.index_array = p_index_array;
#ifdef DEBUG_ENABLED
	dl->validation.index_array_max_index = index_array->max_index;
#endif
	dl->validation.index_array_count = index_array->indices;

	const uint64_t offset_bytes = index_array->offset * (index_array->format == INDEX_BUFFER_FORMAT_UINT16 ? sizeof(uint16_t) : sizeof(uint32_t));
	draw_graph.add_draw_list_bind_index_buffer(index_array->driver_id, index_array->format, offset_bytes);

	if (index_array->draw_tracker != nullptr) {
		draw_graph.add_draw_list_usage(index_array->draw_tracker, RDG::RESOURCE_USAGE_INDEX_BUFFER_READ);
	}
}

void RenderingDevice::draw_list_set_line_width(DrawListID p_list, float p_width) {
	ERR_RENDER_THREAD_GUARD();

	DrawList *dl = _get_draw_list_ptr(p_list);
	ERR_FAIL_NULL(dl);
#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(!dl->validation.active, "Submitted Draw Lists can no longer be modified.");
#endif

	draw_graph.add_draw_list_set_line_width(p_width);
}

void RenderingDevice::draw_list_set_push_constant(DrawListID p_list, const void *p_data, uint32_t p_data_size) {
	ERR_RENDER_THREAD_GUARD();

	DrawList *dl = _get_draw_list_ptr(p_list);
	ERR_FAIL_NULL(dl);

#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(!dl->validation.active, "Submitted Draw Lists can no longer be modified.");
#endif

#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(p_data_size != dl->validation.pipeline_push_constant_size,
			"This render pipeline requires (" + itos(dl->validation.pipeline_push_constant_size) + ") bytes of push constant data, supplied: (" + itos(p_data_size) + ")");
#endif

	draw_graph.add_draw_list_set_push_constant(dl->state.pipeline_shader_driver_id, p_data, p_data_size);

#ifdef DEBUG_ENABLED
	dl->validation.pipeline_push_constant_supplied = true;
#endif
}

void RenderingDevice::draw_list_draw(DrawListID p_list, bool p_use_indices, uint32_t p_instances, uint32_t p_procedural_vertices) {
	// 线程保护宏：确保当前是在合法的渲染线程（上下文）中使用
	ERR_RENDER_THREAD_GUARD();

	// 通过句柄拿到内部的drawlist指针
	DrawList *dl = _get_draw_list_ptr(p_list);
	// 判断drawlist指针是否为空
	ERR_FAIL_NULL(dl);
#ifdef DEBUG_ENABLED
	// DEBUG：已提交的 draw list 不允许再修改或绘制。
	ERR_FAIL_COND_MSG(!dl->validation.active, "Submitted Draw Lists can no longer be modified.");
#endif

#ifdef DEBUG_ENABLED
	// // DEBUG：绘制前必须已经绑定管线（PSO）。
	ERR_FAIL_COND_MSG(!dl->validation.pipeline_active,
			"No render pipeline was set before attempting to draw.");
	// DEBUG：若管线需要顶点输入（有顶点格式）就做进一步校验。
	if (dl->validation.pipeline_vertex_format != INVALID_ID) {
		// Pipeline uses vertices, validate format.
		// DEBUG：管线期望顶点数组，但当前未绑定 VAO/VBO。
		ERR_FAIL_COND_MSG(dl->validation.vertex_format == INVALID_ID,
				"No vertex array was bound, and render pipeline expects vertices.");
		// Make sure format is right.
		// DEBUG：已绑定的顶点格式与创建管线时记录的不一致。
		ERR_FAIL_COND_MSG(dl->validation.pipeline_vertex_format != dl->validation.vertex_format,
				"The vertex format used to create the pipeline does not match the vertex format bound.");
		// Make sure number of instances is valid.
		// DEBUG：实例数超过当前 VAO 支持的最大实例数。
		ERR_FAIL_COND_MSG(p_instances > dl->validation.vertex_max_instances_allowed,
				"Number of instances requested (" + itos(p_instances) + " is larger than the maximum number supported by the bound vertex array (" + itos(dl->validation.vertex_max_instances_allowed) + ").");
	}

	if (dl->validation.pipeline_push_constant_size > 0) {
		// Using push constants, check that they were supplied.
		// DEBUG：管线着色器需要 push constant，但未设置。
		ERR_FAIL_COND_MSG(!dl->validation.pipeline_push_constant_supplied,
				"The shader in this pipeline requires a push constant to be set before drawing, but it's not present.");
	}

#endif

#ifdef DEBUG_ENABLED
	// 遍历所有descriptor set槽位，检查“管线期望的layout与实际绑定的unform集格式”是否一致。
	for (uint32_t i = 0; i < dl->state.set_count; i++) {
		if (dl->state.sets[i].pipeline_expected_format == 0) {
			// Nothing expected by this pipeline.
			// 若该 set 槽位对本管线不需要任何绑定，跳过。
			continue;
		}

		if (dl->state.sets[i].pipeline_expected_format != dl->state.sets[i].uniform_set_format) {
			if (dl->state.sets[i].uniform_set_format == 0) {
				// DEBUG：需要该 set，但从未绑定。
				ERR_FAIL_MSG("Uniforms were never supplied for set (" + itos(i) + ") at the time of drawing, which are required by the pipeline.");
			} else if (uniform_set_owner.owns(dl->state.sets[i].uniform_set)) {
				// DEBUG：当前绑定的 set 与管线着色器声明的 set 格式不一致，打印两边的 layout 便于比对。
				UniformSet *us = uniform_set_owner.get_or_null(dl->state.sets[i].uniform_set);
				ERR_FAIL_MSG("Uniforms supplied for set (" + itos(i) + "):\n" + _shader_uniform_debug(us->shader_id, us->shader_set) + "\nare not the same format as required by the pipeline shader. Pipeline shader requires the following bindings:\n" + _shader_uniform_debug(dl->state.pipeline_shader));
			} else {
				// DEBUG：绑定的 set 已被释放或无效且与期望不符。
				ERR_FAIL_MSG("Uniforms supplied for set (" + itos(i) + ", which was just freed) are not the same format as required by the pipeline shader. Pipeline shader requires the following bindings:\n" + _shader_uniform_debug(dl->state.pipeline_shader));
			}
		}
	}
#endif

	// 下面开始坐descirptor set批绑定的准备（收集连续的set，一次性绑定以减少驱动调用）
	thread_local LocalVector<RDD::UniformSetID> valid_descriptor_ids;	// 线程本地，避免重复分配
	valid_descriptor_ids.clear(); // 清空上一帧/上一次的内容。
	valid_descriptor_ids.resize(dl->state.set_count);	// 预分配到最大 set 数，后面按 valid_set_count 使用。
	uint32_t valid_set_count = 0;	// 当前批次收集到的set的数量
	uint32_t first_set_index = 0;	// 当前批次的起始槽位索引
	uint32_t last_set_index = 0;	// 最近一次假如批次的槽位索引
	bool found_first_set = false;	// 是否已经找到本批次的第一个待绑定的set

	// 预处理：遍历所有set槽位，如果该管线在某个槽位有期望，就可能需要准备它。
	for (uint32_t i = 0; i < dl->state.set_count; i++) {
		if (dl->state.sets[i].pipeline_expected_format == 0) {
			// 对该槽位无需求，跳过。
			continue; // Nothing expected by this pipeline.
		}

		// 槽位还没绑定，并且起始槽位还为空
		if (!dl->state.sets[i].bound && !found_first_set) {
			// 记录第一个尚未绑定的槽位作为批次起点
			first_set_index = i;
			found_first_set = true;
		}
		// Prepare descriptor sets if the API doesn't use pipeline barriers.
		// 如果底层API不支持自动处理管线屏障，需要提前将set设置为待用，让驱动做必要的资源状态迁移
		if (!driver->api_trait_get(RDD::API_TRAIT_HONORS_PIPELINE_BARRIERS)) {
			draw_graph.add_draw_list_uniform_set_prepare_for_use(dl->state.pipeline_shader_driver_id, dl->state.sets[i].uniform_set_driver_id, i);
		}
	}

	// Bind descriptor sets.
	// 真正绑定描述符集，连续槽位合并为一次绑定调用
	for (uint32_t i = first_set_index; i < dl->state.set_count; i++) {
		if (dl->state.sets[i].pipeline_expected_format == 0) {
			continue; // Nothing expected by this pipeline.		// 该槽位对本管线无需求，跳过
		}

		// 只处理尚未绑定的槽位
		if (!dl->state.sets[i].bound) {
			// Batch contiguous descriptor sets in a single call.
			// 如果启用了批绑定
			if (descriptor_set_batching) {
				// All good, see if this requires re-binding.
				// 挺好，看看是否需要重新绑定
				if (i - last_set_index > 1) {	// 与上一个收集到的槽位不连续，则把之前批次提交并重开新批次。
					// If the descriptor sets are not contiguous, bind the previous ones and start a new batch.
					draw_graph.add_draw_list_bind_uniform_sets(dl->state.pipeline_shader_driver_id, valid_descriptor_ids, first_set_index, valid_set_count);
					// 提交上一个批次的绑定命令。

					first_set_index = i;	// 新批次起点为当前 i。
					valid_set_count = 1;	// 新批次计数归 1。
					valid_descriptor_ids[0] = dl->state.sets[i].uniform_set_driver_id;	// 记录当前 set 的底层驱动 ID。
				} else {
					// Otherwise, keep storing in the current batch.
					// 槽位连续，加入当前批次。
					valid_descriptor_ids[valid_set_count] = dl->state.sets[i].uniform_set_driver_id;
					valid_set_count++;	// 集数量+1
				}

				// 取到高层 UniformSet 对象指针。
				UniformSet *uniform_set = uniform_set_owner.get_or_null(dl->state.sets[i].uniform_set);
				// 确保共享资源/动态内容已经更新
				_uniform_set_update_shared(uniform_set);
				// 记录本次绘制对该 uniform set 的使用（用于同步/资源生命周期追踪）。
				draw_graph.add_draw_list_usages(uniform_set->draw_trackers, uniform_set->draw_trackers_usage);
				dl->state.sets[i].bound = true;		// 标记为已绑定

				last_set_index = i;		// 记录最近假如批次的槽位索引
			} else {
				// 不启用批次，逐个绑定（但是开销更大，优点是逻辑简单）
				draw_graph.add_draw_list_bind_uniform_set(dl->state.pipeline_shader_driver_id, dl->state.sets[i].uniform_set_driver_id, i);
			}
		}
	}

	// Bind the remaining batch.
	// 把最后一个未提交的批次提交
	if (descriptor_set_batching && valid_set_count > 0) {
		draw_graph.add_draw_list_bind_uniform_sets(dl->state.pipeline_shader_driver_id, valid_descriptor_ids, first_set_index, valid_set_count);
	}

	// 接下来根据是否使用索引来决定调用哪类draw
	if (p_use_indices) {
#ifdef DEBUG_ENABLED
		// DEBUG：索引绘制与“程序化顶点计数”不能同时使用。
		ERR_FAIL_COND_MSG(p_procedural_vertices > 0,
				"Procedural vertices can't be used together with indices.");

		// DEBUG：请求索引绘制，但未绑定索引缓冲。
		ERR_FAIL_COND_MSG(!dl->validation.index_array_count,
				"Draw command requested indices, but no index buffer was set.");

		// DEBUG：索引重启特性（primitive restart）配置不匹配。
		ERR_FAIL_COND_MSG(dl->validation.pipeline_uses_restart_indices != dl->validation.index_buffer_uses_restart_indices,
				"The usage of restart indices in index buffer does not match the render primitive in the pipeline.");
#endif
		// 根据当前移植的索引数量作为绘制数量
		uint32_t to_draw = dl->validation.index_array_count;

#ifdef DEBUG_ENABLED
		// DEBUG：索引数不足以构成至少一个图元。
		ERR_FAIL_COND_MSG(to_draw < dl->validation.pipeline_primitive_minimum,
				"Too few indices (" + itos(to_draw) + ") for the render primitive set in the render pipeline (" + itos(dl->validation.pipeline_primitive_minimum) + ").");

		// DEBUG：索引数必须是图元所需索引数的整数倍（例如三角形=3，线段=2）。
		ERR_FAIL_COND_MSG((to_draw % dl->validation.pipeline_primitive_divisor) != 0,
				"Index amount (" + itos(to_draw) + ") must be a multiple of the amount of indices required by the render primitive (" + itos(dl->validation.pipeline_primitive_divisor) + ").");
#endif

		// 记录一次“索引绘制”指令：数量、实例数、起始偏移
		draw_graph.add_draw_list_draw_indexed(to_draw, p_instances, 0);
	} else {
		// 非索引绘制，记录顶点数
		uint32_t to_draw;

		// 如果指定了“程序化顶点数”，则不依赖顶点数组
		if (p_procedural_vertices > 0) {
#ifdef DEBUG_ENABLED
			// DEBUG：程序化绘制不应搭配需要顶点数组的管线。
			ERR_FAIL_COND_MSG(dl->validation.pipeline_vertex_format != INVALID_ID,
					"Procedural vertices requested, but pipeline expects a vertex array.");
#endif
			// 直接用传入的程序化顶点数
			to_draw = p_procedural_vertices;
		} else {
#ifdef DEBUG_ENABLED
			// DEBUG：非索引绘制但管线也不使用顶点（矛盾）。
			ERR_FAIL_COND_MSG(dl->validation.pipeline_vertex_format == INVALID_ID,
					"Draw command lacks indices, but pipeline format does not use vertices.");
#endif
			// 使用已绑定 VAO 的顶点数。
			to_draw = dl->validation.vertex_array_size;
		}

#ifdef DEBUG_ENABLED
		// DEBUG：顶点数不足以构成至少一个图元。
		ERR_FAIL_COND_MSG(to_draw < dl->validation.pipeline_primitive_minimum,
				"Too few vertices (" + itos(to_draw) + ") for the render primitive set in the render pipeline (" + itos(dl->validation.pipeline_primitive_minimum) + ").");

		// DEBUG：顶点数必须是每个图元需求顶点数的整数倍。
		ERR_FAIL_COND_MSG((to_draw % dl->validation.pipeline_primitive_divisor) != 0,
				"Vertex amount (" + itos(to_draw) + ") must be a multiple of the amount of vertices required by the render primitive (" + itos(dl->validation.pipeline_primitive_divisor) + ").");
#endif

		// 记录一次“非索引绘制”指令：顶点数、实例数
		draw_graph.add_draw_list_draw(to_draw, p_instances);
	}

	// 统计：本 draw list 的已记录绘制次数 +1。
	dl->state.draw_count++;
}

void RenderingDevice::draw_list_draw_indirect(DrawListID p_list, bool p_use_indices, RID p_buffer, uint32_t p_offset, uint32_t p_draw_count, uint32_t p_stride) {
	ERR_RENDER_THREAD_GUARD();

	DrawList *dl = _get_draw_list_ptr(p_list);
	ERR_FAIL_NULL(dl);

	Buffer *buffer = storage_buffer_owner.get_or_null(p_buffer);
	ERR_FAIL_NULL(buffer);

	ERR_FAIL_COND_MSG(!buffer->usage.has_flag(RDD::BUFFER_USAGE_INDIRECT_BIT), "Buffer provided was not created to do indirect dispatch.");

#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(!dl->validation.active, "Submitted Draw Lists can no longer be modified.");
#endif

#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(!dl->validation.pipeline_active,
			"No render pipeline was set before attempting to draw.");
	if (dl->validation.pipeline_vertex_format != INVALID_ID) {
		// Pipeline uses vertices, validate format.
		ERR_FAIL_COND_MSG(dl->validation.vertex_format == INVALID_ID,
				"No vertex array was bound, and render pipeline expects vertices.");
		// Make sure format is right.
		ERR_FAIL_COND_MSG(dl->validation.pipeline_vertex_format != dl->validation.vertex_format,
				"The vertex format used to create the pipeline does not match the vertex format bound.");
	}

	if (dl->validation.pipeline_push_constant_size > 0) {
		// Using push constants, check that they were supplied.
		ERR_FAIL_COND_MSG(!dl->validation.pipeline_push_constant_supplied,
				"The shader in this pipeline requires a push constant to be set before drawing, but it's not present.");
	}
#endif

#ifdef DEBUG_ENABLED
	for (uint32_t i = 0; i < dl->state.set_count; i++) {
		if (dl->state.sets[i].pipeline_expected_format == 0) {
			// Nothing expected by this pipeline.
			continue;
		}

		if (dl->state.sets[i].pipeline_expected_format != dl->state.sets[i].uniform_set_format) {
			if (dl->state.sets[i].uniform_set_format == 0) {
				ERR_FAIL_MSG(vformat("Uniforms were never supplied for set (%d) at the time of drawing, which are required by the pipeline.", i));
			} else if (uniform_set_owner.owns(dl->state.sets[i].uniform_set)) {
				UniformSet *us = uniform_set_owner.get_or_null(dl->state.sets[i].uniform_set);
				ERR_FAIL_MSG(vformat("Uniforms supplied for set (%d):\n%s\nare not the same format as required by the pipeline shader. Pipeline shader requires the following bindings:\n%s", i, _shader_uniform_debug(us->shader_id, us->shader_set), _shader_uniform_debug(dl->state.pipeline_shader)));
			} else {
				ERR_FAIL_MSG(vformat("Uniforms supplied for set (%s, which was just freed) are not the same format as required by the pipeline shader. Pipeline shader requires the following bindings:\n%s", i, _shader_uniform_debug(dl->state.pipeline_shader)));
			}
		}
	}
#endif

	// Prepare descriptor sets if the API doesn't use pipeline barriers.
	if (!driver->api_trait_get(RDD::API_TRAIT_HONORS_PIPELINE_BARRIERS)) {
		for (uint32_t i = 0; i < dl->state.set_count; i++) {
			if (dl->state.sets[i].pipeline_expected_format == 0) {
				// Nothing expected by this pipeline.
				continue;
			}

			draw_graph.add_draw_list_uniform_set_prepare_for_use(dl->state.pipeline_shader_driver_id, dl->state.sets[i].uniform_set_driver_id, i);
		}
	}

	// Bind descriptor sets.
	for (uint32_t i = 0; i < dl->state.set_count; i++) {
		if (dl->state.sets[i].pipeline_expected_format == 0) {
			continue; // Nothing expected by this pipeline.
		}
		if (!dl->state.sets[i].bound) {
			// All good, see if this requires re-binding.
			draw_graph.add_draw_list_bind_uniform_set(dl->state.pipeline_shader_driver_id, dl->state.sets[i].uniform_set_driver_id, i);

			UniformSet *uniform_set = uniform_set_owner.get_or_null(dl->state.sets[i].uniform_set);
			_uniform_set_update_shared(uniform_set);

			draw_graph.add_draw_list_usages(uniform_set->draw_trackers, uniform_set->draw_trackers_usage);

			dl->state.sets[i].bound = true;
		}
	}

	if (p_use_indices) {
#ifdef DEBUG_ENABLED
		ERR_FAIL_COND_MSG(!dl->validation.index_array_count,
				"Draw command requested indices, but no index buffer was set.");

		ERR_FAIL_COND_MSG(dl->validation.pipeline_uses_restart_indices != dl->validation.index_buffer_uses_restart_indices,
				"The usage of restart indices in index buffer does not match the render primitive in the pipeline.");
#endif

		ERR_FAIL_COND_MSG(p_offset + 20 > buffer->size, "Offset provided (+20) is past the end of buffer.");

		draw_graph.add_draw_list_draw_indexed_indirect(buffer->driver_id, p_offset, p_draw_count, p_stride);
	} else {
		ERR_FAIL_COND_MSG(p_offset + 16 > buffer->size, "Offset provided (+16) is past the end of buffer.");

		draw_graph.add_draw_list_draw_indirect(buffer->driver_id, p_offset, p_draw_count, p_stride);
	}

	dl->state.draw_count++;

	if (buffer->draw_tracker != nullptr) {
		draw_graph.add_draw_list_usage(buffer->draw_tracker, RDG::RESOURCE_USAGE_INDIRECT_BUFFER_READ);
	}

	_check_transfer_worker_buffer(buffer);
}

void RenderingDevice::draw_list_set_viewport(DrawListID p_list, const Rect2 &p_rect) {
	DrawList *dl = _get_draw_list_ptr(p_list);

	ERR_FAIL_NULL(dl);
#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(!dl->validation.active, "Submitted Draw Lists can no longer be modified.");
#endif

	if (p_rect.get_area() == 0) {
		return;
	}

	dl->viewport = p_rect;
	draw_graph.add_draw_list_set_viewport(p_rect);
}

void RenderingDevice::draw_list_enable_scissor(DrawListID p_list, const Rect2 &p_rect) {
	ERR_RENDER_THREAD_GUARD();

	DrawList *dl = _get_draw_list_ptr(p_list);

	ERR_FAIL_NULL(dl);
#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(!dl->validation.active, "Submitted Draw Lists can no longer be modified.");
#endif
	Rect2i rect = p_rect;
	rect.position += dl->viewport.position;

	rect = dl->viewport.intersection(rect);

	if (rect.get_area() == 0) {
		return;
	}

	draw_graph.add_draw_list_set_scissor(rect);
}

void RenderingDevice::draw_list_disable_scissor(DrawListID p_list) {
	ERR_RENDER_THREAD_GUARD();

	DrawList *dl = _get_draw_list_ptr(p_list);
	ERR_FAIL_NULL(dl);
#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(!dl->validation.active, "Submitted Draw Lists can no longer be modified.");
#endif

	draw_graph.add_draw_list_set_scissor(dl->viewport);
}

uint32_t RenderingDevice::draw_list_get_current_pass() {
	ERR_RENDER_THREAD_GUARD_V(0);

	return draw_list_current_subpass;
}

RenderingDevice::DrawListID RenderingDevice::draw_list_switch_to_next_pass() {
	ERR_RENDER_THREAD_GUARD_V(INVALID_ID);

	ERR_FAIL_NULL_V(draw_list, INVALID_ID);
	ERR_FAIL_COND_V(draw_list_current_subpass >= draw_list_subpass_count - 1, INVALID_FORMAT_ID);

	draw_list_current_subpass++;

	Rect2i viewport;
	_draw_list_free(&viewport);

	draw_graph.add_draw_list_next_subpass(RDD::COMMAND_BUFFER_TYPE_PRIMARY);

	_draw_list_allocate(viewport, draw_list_current_subpass);

	return int64_t(ID_TYPE_DRAW_LIST) << ID_BASE_SHIFT;
}

#ifndef DISABLE_DEPRECATED
Error RenderingDevice::draw_list_switch_to_next_pass_split(uint32_t p_splits, DrawListID *r_split_ids) {
	ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "Deprecated. Split draw lists are used automatically by RenderingDevice.");
}
#endif

Error RenderingDevice::_draw_list_allocate(const Rect2i &p_viewport, uint32_t p_subpass) {
	draw_list = memnew(DrawList);
	draw_list->viewport = p_viewport;

	return OK;
}

void RenderingDevice::_draw_list_free(Rect2i *r_last_viewport) {
	if (r_last_viewport) {
		*r_last_viewport = draw_list->viewport;
	}
	// Just end the list.
	memdelete(draw_list);
	draw_list = nullptr;
}

void RenderingDevice::draw_list_end() {
	ERR_RENDER_THREAD_GUARD();

	ERR_FAIL_NULL_MSG(draw_list, "Immediate draw list is already inactive.");

	draw_graph.add_draw_list_end();

	_draw_list_free();

	for (int i = 0; i < draw_list_bound_textures.size(); i++) {
		Texture *texture = texture_owner.get_or_null(draw_list_bound_textures[i]);
		ERR_CONTINUE(!texture); // Wtf.
		if (texture->usage_flags & TEXTURE_USAGE_COLOR_ATTACHMENT_BIT) {
			texture->bound = false;
		}
		if (texture->usage_flags & TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			texture->bound = false;
		}
	}

	draw_list_bound_textures.clear();
}

/***********************/
/**** COMPUTE LISTS ****/
/***********************/

RenderingDevice::ComputeListID RenderingDevice::compute_list_begin() {
	ERR_RENDER_THREAD_GUARD_V(INVALID_ID);

	ERR_FAIL_COND_V_MSG(compute_list != nullptr, INVALID_ID, "Only one draw/compute list can be active at the same time.");

	compute_list = memnew(ComputeList);

	draw_graph.add_compute_list_begin();

	return ID_TYPE_COMPUTE_LIST;
}

void RenderingDevice::compute_list_bind_compute_pipeline(ComputeListID p_list, RID p_compute_pipeline) {
	ERR_RENDER_THREAD_GUARD();

	ERR_FAIL_COND(p_list != ID_TYPE_COMPUTE_LIST);
	ERR_FAIL_NULL(compute_list);

	ComputeList *cl = compute_list;

	const ComputePipeline *pipeline = compute_pipeline_owner.get_or_null(p_compute_pipeline);
	ERR_FAIL_NULL(pipeline);

	if (p_compute_pipeline == cl->state.pipeline) {
		return; // Redundant state, return.
	}

	cl->state.pipeline = p_compute_pipeline;

	draw_graph.add_compute_list_bind_pipeline(pipeline->driver_id);

	if (cl->state.pipeline_shader != pipeline->shader) {
		// Shader changed, so descriptor sets may become incompatible.

		uint32_t pcount = pipeline->set_formats.size(); // Formats count in this pipeline.
		cl->state.set_count = MAX(cl->state.set_count, pcount);
		const uint32_t *pformats = pipeline->set_formats.ptr(); // Pipeline set formats.

		uint32_t first_invalid_set = UINT32_MAX; // All valid by default.
		switch (driver->api_trait_get(RDD::API_TRAIT_SHADER_CHANGE_INVALIDATION)) {
			case RDD::SHADER_CHANGE_INVALIDATION_ALL_BOUND_UNIFORM_SETS: {
				first_invalid_set = 0;
			} break;
			case RDD::SHADER_CHANGE_INVALIDATION_INCOMPATIBLE_SETS_PLUS_CASCADE: {
				for (uint32_t i = 0; i < pcount; i++) {
					if (cl->state.sets[i].pipeline_expected_format != pformats[i]) {
						first_invalid_set = i;
						break;
					}
				}
			} break;
			case RDD::SHADER_CHANGE_INVALIDATION_ALL_OR_NONE_ACCORDING_TO_LAYOUT_HASH: {
				if (cl->state.pipeline_shader_layout_hash != pipeline->shader_layout_hash) {
					first_invalid_set = 0;
				}
			} break;
		}

		for (uint32_t i = 0; i < pcount; i++) {
			cl->state.sets[i].bound = cl->state.sets[i].bound && i < first_invalid_set;
			cl->state.sets[i].pipeline_expected_format = pformats[i];
		}

		for (uint32_t i = pcount; i < cl->state.set_count; i++) {
			// Unbind the ones above (not used) if exist.
			cl->state.sets[i].bound = false;
		}

		cl->state.set_count = pcount; // Update set count.

		if (pipeline->push_constant_size) {
#ifdef DEBUG_ENABLED
			cl->validation.pipeline_push_constant_supplied = false;
#endif
		}

		cl->state.pipeline_shader = pipeline->shader;
		cl->state.pipeline_shader_driver_id = pipeline->shader_driver_id;
		cl->state.pipeline_shader_layout_hash = pipeline->shader_layout_hash;
		cl->state.local_group_size[0] = pipeline->local_group_size[0];
		cl->state.local_group_size[1] = pipeline->local_group_size[1];
		cl->state.local_group_size[2] = pipeline->local_group_size[2];
	}

#ifdef DEBUG_ENABLED
	// Update compute pass pipeline info.
	cl->validation.pipeline_active = true;
	cl->validation.pipeline_push_constant_size = pipeline->push_constant_size;
#endif
}

void RenderingDevice::compute_list_bind_uniform_set(ComputeListID p_list, RID p_uniform_set, uint32_t p_index) {
	ERR_RENDER_THREAD_GUARD();

	ERR_FAIL_COND(p_list != ID_TYPE_COMPUTE_LIST);
	ERR_FAIL_NULL(compute_list);

	ComputeList *cl = compute_list;

#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(p_index >= driver->limit_get(LIMIT_MAX_BOUND_UNIFORM_SETS) || p_index >= MAX_UNIFORM_SETS,
			"Attempting to bind a descriptor set (" + itos(p_index) + ") greater than what the hardware supports (" + itos(driver->limit_get(LIMIT_MAX_BOUND_UNIFORM_SETS)) + ").");
#endif

#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(!cl->validation.active, "Submitted Compute Lists can no longer be modified.");
#endif

	UniformSet *uniform_set = uniform_set_owner.get_or_null(p_uniform_set);
	ERR_FAIL_NULL(uniform_set);

	if (p_index > cl->state.set_count) {
		cl->state.set_count = p_index;
	}

	cl->state.sets[p_index].uniform_set_driver_id = uniform_set->driver_id; // Update set pointer.
	cl->state.sets[p_index].bound = false; // Needs rebind.
	cl->state.sets[p_index].uniform_set_format = uniform_set->format;
	cl->state.sets[p_index].uniform_set = p_uniform_set;

#if 0
	{ // Validate that textures bound are not attached as framebuffer bindings.
		uint32_t attachable_count = uniform_set->attachable_textures.size();
		const RID *attachable_ptr = uniform_set->attachable_textures.ptr();
		uint32_t bound_count = draw_list_bound_textures.size();
		const RID *bound_ptr = draw_list_bound_textures.ptr();
		for (uint32_t i = 0; i < attachable_count; i++) {
			for (uint32_t j = 0; j < bound_count; j++) {
				ERR_FAIL_COND_MSG(attachable_ptr[i] == bound_ptr[j],
						"Attempted to use the same texture in framebuffer attachment and a uniform set, this is not allowed.");
			}
		}
	}
#endif
}

void RenderingDevice::compute_list_set_push_constant(ComputeListID p_list, const void *p_data, uint32_t p_data_size) {
	ERR_RENDER_THREAD_GUARD();

	ERR_FAIL_COND(p_list != ID_TYPE_COMPUTE_LIST);
	ERR_FAIL_NULL(compute_list);
	ERR_FAIL_COND_MSG(p_data_size > MAX_PUSH_CONSTANT_SIZE, "Push constants can't be bigger than 128 bytes to maintain compatibility.");

	ComputeList *cl = compute_list;

#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(!cl->validation.active, "Submitted Compute Lists can no longer be modified.");
#endif

#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(p_data_size != cl->validation.pipeline_push_constant_size,
			"This compute pipeline requires (" + itos(cl->validation.pipeline_push_constant_size) + ") bytes of push constant data, supplied: (" + itos(p_data_size) + ")");
#endif

	draw_graph.add_compute_list_set_push_constant(cl->state.pipeline_shader_driver_id, p_data, p_data_size);

	// Store it in the state in case we need to restart the compute list.
	memcpy(cl->state.push_constant_data, p_data, p_data_size);
	cl->state.push_constant_size = p_data_size;

#ifdef DEBUG_ENABLED
	cl->validation.pipeline_push_constant_supplied = true;
#endif
}

void RenderingDevice::compute_list_dispatch(ComputeListID p_list, uint32_t p_x_groups, uint32_t p_y_groups, uint32_t p_z_groups) {
	ERR_RENDER_THREAD_GUARD();

	ERR_FAIL_COND(p_list != ID_TYPE_COMPUTE_LIST);
	ERR_FAIL_NULL(compute_list);

	ComputeList *cl = compute_list;

#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(p_x_groups == 0, "Dispatch amount of X compute groups (" + itos(p_x_groups) + ") is zero.");
	ERR_FAIL_COND_MSG(p_z_groups == 0, "Dispatch amount of Z compute groups (" + itos(p_z_groups) + ") is zero.");
	ERR_FAIL_COND_MSG(p_y_groups == 0, "Dispatch amount of Y compute groups (" + itos(p_y_groups) + ") is zero.");
	ERR_FAIL_COND_MSG(p_x_groups > driver->limit_get(LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X),
			"Dispatch amount of X compute groups (" + itos(p_x_groups) + ") is larger than device limit (" + itos(driver->limit_get(LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X)) + ")");
	ERR_FAIL_COND_MSG(p_y_groups > driver->limit_get(LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_Y),
			"Dispatch amount of Y compute groups (" + itos(p_y_groups) + ") is larger than device limit (" + itos(driver->limit_get(LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_Y)) + ")");
	ERR_FAIL_COND_MSG(p_z_groups > driver->limit_get(LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_Z),
			"Dispatch amount of Z compute groups (" + itos(p_z_groups) + ") is larger than device limit (" + itos(driver->limit_get(LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_Z)) + ")");

	ERR_FAIL_COND_MSG(!cl->validation.active, "Submitted Compute Lists can no longer be modified.");
#endif

#ifdef DEBUG_ENABLED

	ERR_FAIL_COND_MSG(!cl->validation.pipeline_active, "No compute pipeline was set before attempting to draw.");

	if (cl->validation.pipeline_push_constant_size > 0) {
		// Using push constants, check that they were supplied.
		ERR_FAIL_COND_MSG(!cl->validation.pipeline_push_constant_supplied,
				"The shader in this pipeline requires a push constant to be set before drawing, but it's not present.");
	}

#endif

#ifdef DEBUG_ENABLED
	for (uint32_t i = 0; i < cl->state.set_count; i++) {
		if (cl->state.sets[i].pipeline_expected_format == 0) {
			// Nothing expected by this pipeline.
			continue;
		}

		if (cl->state.sets[i].pipeline_expected_format != cl->state.sets[i].uniform_set_format) {
			if (cl->state.sets[i].uniform_set_format == 0) {
				ERR_FAIL_MSG("Uniforms were never supplied for set (" + itos(i) + ") at the time of drawing, which are required by the pipeline.");
			} else if (uniform_set_owner.owns(cl->state.sets[i].uniform_set)) {
				UniformSet *us = uniform_set_owner.get_or_null(cl->state.sets[i].uniform_set);
				ERR_FAIL_MSG("Uniforms supplied for set (" + itos(i) + "):\n" + _shader_uniform_debug(us->shader_id, us->shader_set) + "\nare not the same format as required by the pipeline shader. Pipeline shader requires the following bindings:\n" + _shader_uniform_debug(cl->state.pipeline_shader));
			} else {
				ERR_FAIL_MSG("Uniforms supplied for set (" + itos(i) + ", which was just freed) are not the same format as required by the pipeline shader. Pipeline shader requires the following bindings:\n" + _shader_uniform_debug(cl->state.pipeline_shader));
			}
		}
	}
#endif
	thread_local LocalVector<RDD::UniformSetID> valid_descriptor_ids;
	valid_descriptor_ids.clear();
	valid_descriptor_ids.resize(cl->state.set_count);

	uint32_t valid_set_count = 0;
	uint32_t first_set_index = 0;
	uint32_t last_set_index = 0;
	bool found_first_set = false;

	for (uint32_t i = 0; i < cl->state.set_count; i++) {
		if (cl->state.sets[i].pipeline_expected_format == 0) {
			// Nothing expected by this pipeline.
			continue;
		}

		if (!cl->state.sets[i].bound && !found_first_set) {
			first_set_index = i;
			found_first_set = true;
		}
		// Prepare descriptor sets if the API doesn't use pipeline barriers.
		if (!driver->api_trait_get(RDD::API_TRAIT_HONORS_PIPELINE_BARRIERS)) {
			draw_graph.add_compute_list_uniform_set_prepare_for_use(cl->state.pipeline_shader_driver_id, cl->state.sets[i].uniform_set_driver_id, i);
		}
	}

	// Bind descriptor sets.
	for (uint32_t i = first_set_index; i < cl->state.set_count; i++) {
		if (cl->state.sets[i].pipeline_expected_format == 0) {
			continue; // Nothing expected by this pipeline.
		}

		if (!cl->state.sets[i].bound) {
			// Descriptor set batching
			if (descriptor_set_batching) {
				// All good, see if this requires re-binding.
				if (i - last_set_index > 1) {
					// If the descriptor sets are not contiguous, bind the previous ones and start a new batch.
					draw_graph.add_compute_list_bind_uniform_sets(cl->state.pipeline_shader_driver_id, valid_descriptor_ids, first_set_index, valid_set_count);

					first_set_index = i;
					valid_set_count = 1;
					valid_descriptor_ids[0] = cl->state.sets[i].uniform_set_driver_id;
				} else {
					// Otherwise, keep storing in the current batch.
					valid_descriptor_ids[valid_set_count] = cl->state.sets[i].uniform_set_driver_id;
					valid_set_count++;
				}

				last_set_index = i;
			} else {
				draw_graph.add_compute_list_bind_uniform_set(cl->state.pipeline_shader_driver_id, cl->state.sets[i].uniform_set_driver_id, i);
			}
			UniformSet *uniform_set = uniform_set_owner.get_or_null(cl->state.sets[i].uniform_set);
			_uniform_set_update_shared(uniform_set);

			draw_graph.add_compute_list_usages(uniform_set->draw_trackers, uniform_set->draw_trackers_usage);
			cl->state.sets[i].bound = true;
		}
	}

	// Bind the remaining batch.
	if (valid_set_count > 0) {
		draw_graph.add_compute_list_bind_uniform_sets(cl->state.pipeline_shader_driver_id, valid_descriptor_ids, first_set_index, valid_set_count);
	}
	draw_graph.add_compute_list_dispatch(p_x_groups, p_y_groups, p_z_groups);
	cl->state.dispatch_count++;
}

void RenderingDevice::compute_list_dispatch_threads(ComputeListID p_list, uint32_t p_x_threads, uint32_t p_y_threads, uint32_t p_z_threads) {
	ERR_RENDER_THREAD_GUARD();

	ERR_FAIL_COND(p_list != ID_TYPE_COMPUTE_LIST);
	ERR_FAIL_NULL(compute_list);

#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(p_x_threads == 0, "Dispatch amount of X compute threads (" + itos(p_x_threads) + ") is zero.");
	ERR_FAIL_COND_MSG(p_y_threads == 0, "Dispatch amount of Y compute threads (" + itos(p_y_threads) + ") is zero.");
	ERR_FAIL_COND_MSG(p_z_threads == 0, "Dispatch amount of Z compute threads (" + itos(p_z_threads) + ") is zero.");
#endif

	ComputeList *cl = compute_list;

#ifdef DEBUG_ENABLED

	ERR_FAIL_COND_MSG(!cl->validation.pipeline_active, "No compute pipeline was set before attempting to draw.");

	if (cl->validation.pipeline_push_constant_size > 0) {
		// Using push constants, check that they were supplied.
		ERR_FAIL_COND_MSG(!cl->validation.pipeline_push_constant_supplied,
				"The shader in this pipeline requires a push constant to be set before drawing, but it's not present.");
	}

#endif

	compute_list_dispatch(p_list, Math::division_round_up(p_x_threads, cl->state.local_group_size[0]), Math::division_round_up(p_y_threads, cl->state.local_group_size[1]), Math::division_round_up(p_z_threads, cl->state.local_group_size[2]));
}

void RenderingDevice::compute_list_dispatch_indirect(ComputeListID p_list, RID p_buffer, uint32_t p_offset) {
	ERR_RENDER_THREAD_GUARD();

	ERR_FAIL_COND(p_list != ID_TYPE_COMPUTE_LIST);
	ERR_FAIL_NULL(compute_list);

	ComputeList *cl = compute_list;
	Buffer *buffer = storage_buffer_owner.get_or_null(p_buffer);
	ERR_FAIL_NULL(buffer);

	ERR_FAIL_COND_MSG(!buffer->usage.has_flag(RDD::BUFFER_USAGE_INDIRECT_BIT), "Buffer provided was not created to do indirect dispatch.");

	ERR_FAIL_COND_MSG(p_offset + 12 > buffer->size, "Offset provided (+12) is past the end of buffer.");

#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(!cl->validation.active, "Submitted Compute Lists can no longer be modified.");
#endif

#ifdef DEBUG_ENABLED

	ERR_FAIL_COND_MSG(!cl->validation.pipeline_active, "No compute pipeline was set before attempting to draw.");

	if (cl->validation.pipeline_push_constant_size > 0) {
		// Using push constants, check that they were supplied.
		ERR_FAIL_COND_MSG(!cl->validation.pipeline_push_constant_supplied,
				"The shader in this pipeline requires a push constant to be set before drawing, but it's not present.");
	}

#endif

#ifdef DEBUG_ENABLED
	for (uint32_t i = 0; i < cl->state.set_count; i++) {
		if (cl->state.sets[i].pipeline_expected_format == 0) {
			// Nothing expected by this pipeline.
			continue;
		}

		if (cl->state.sets[i].pipeline_expected_format != cl->state.sets[i].uniform_set_format) {
			if (cl->state.sets[i].uniform_set_format == 0) {
				ERR_FAIL_MSG("Uniforms were never supplied for set (" + itos(i) + ") at the time of drawing, which are required by the pipeline.");
			} else if (uniform_set_owner.owns(cl->state.sets[i].uniform_set)) {
				UniformSet *us = uniform_set_owner.get_or_null(cl->state.sets[i].uniform_set);
				ERR_FAIL_MSG("Uniforms supplied for set (" + itos(i) + "):\n" + _shader_uniform_debug(us->shader_id, us->shader_set) + "\nare not the same format as required by the pipeline shader. Pipeline shader requires the following bindings:\n" + _shader_uniform_debug(cl->state.pipeline_shader));
			} else {
				ERR_FAIL_MSG("Uniforms supplied for set (" + itos(i) + ", which was just freed) are not the same format as required by the pipeline shader. Pipeline shader requires the following bindings:\n" + _shader_uniform_debug(cl->state.pipeline_shader));
			}
		}
	}
#endif
	thread_local LocalVector<RDD::UniformSetID> valid_descriptor_ids;
	valid_descriptor_ids.clear();
	valid_descriptor_ids.resize(cl->state.set_count);

	uint32_t valid_set_count = 0;
	uint32_t first_set_index = 0;
	uint32_t last_set_index = 0;
	bool found_first_set = false;

	for (uint32_t i = 0; i < cl->state.set_count; i++) {
		if (cl->state.sets[i].pipeline_expected_format == 0) {
			// Nothing expected by this pipeline.
			continue;
		}

		if (!cl->state.sets[i].bound && !found_first_set) {
			first_set_index = i;
			found_first_set = true;
		}

		// Prepare descriptor sets if the API doesn't use pipeline barriers.
		if (!driver->api_trait_get(RDD::API_TRAIT_HONORS_PIPELINE_BARRIERS)) {
			draw_graph.add_compute_list_uniform_set_prepare_for_use(cl->state.pipeline_shader_driver_id, cl->state.sets[i].uniform_set_driver_id, i);
		}
	}

	// Bind descriptor sets.
	for (uint32_t i = first_set_index; i < cl->state.set_count; i++) {
		if (cl->state.sets[i].pipeline_expected_format == 0) {
			continue; // Nothing expected by this pipeline.
		}

		if (!cl->state.sets[i].bound) {
			// All good, see if this requires re-binding.
			if (i - last_set_index > 1) {
				// If the descriptor sets are not contiguous, bind the previous ones and start a new batch.
				draw_graph.add_compute_list_bind_uniform_sets(cl->state.pipeline_shader_driver_id, valid_descriptor_ids, first_set_index, valid_set_count);

				first_set_index = i;
				valid_set_count = 1;
				valid_descriptor_ids[0] = cl->state.sets[i].uniform_set_driver_id;
			} else {
				// Otherwise, keep storing in the current batch.
				valid_descriptor_ids[valid_set_count] = cl->state.sets[i].uniform_set_driver_id;
				valid_set_count++;
			}

			last_set_index = i;

			UniformSet *uniform_set = uniform_set_owner.get_or_null(cl->state.sets[i].uniform_set);
			_uniform_set_update_shared(uniform_set);

			draw_graph.add_compute_list_usages(uniform_set->draw_trackers, uniform_set->draw_trackers_usage);
			cl->state.sets[i].bound = true;
		}
	}

	// Bind the remaining batch.
	if (valid_set_count > 0) {
		draw_graph.add_compute_list_bind_uniform_sets(cl->state.pipeline_shader_driver_id, valid_descriptor_ids, first_set_index, valid_set_count);
	}

	draw_graph.add_compute_list_dispatch_indirect(buffer->driver_id, p_offset);
	cl->state.dispatch_count++;

	if (buffer->draw_tracker != nullptr) {
		draw_graph.add_compute_list_usage(buffer->draw_tracker, RDG::RESOURCE_USAGE_INDIRECT_BUFFER_READ);
	}

	_check_transfer_worker_buffer(buffer);
}

void RenderingDevice::compute_list_add_barrier(ComputeListID p_list) {
	ERR_RENDER_THREAD_GUARD();

	compute_list_barrier_state = compute_list->state;
	compute_list_end();
	compute_list_begin();

	if (compute_list_barrier_state.pipeline.is_valid()) {
		compute_list_bind_compute_pipeline(p_list, compute_list_barrier_state.pipeline);
	}

	for (uint32_t i = 0; i < compute_list_barrier_state.set_count; i++) {
		if (compute_list_barrier_state.sets[i].uniform_set.is_valid()) {
			compute_list_bind_uniform_set(p_list, compute_list_barrier_state.sets[i].uniform_set, i);
		}
	}

	if (compute_list_barrier_state.push_constant_size > 0) {
		compute_list_set_push_constant(p_list, compute_list_barrier_state.push_constant_data, compute_list_barrier_state.push_constant_size);
	}
}

void RenderingDevice::compute_list_end() {
	ERR_RENDER_THREAD_GUARD();

	ERR_FAIL_NULL(compute_list);

	draw_graph.add_compute_list_end();

	memdelete(compute_list);
	compute_list = nullptr;
}

#ifndef DISABLE_DEPRECATED
void RenderingDevice::barrier(BitField<BarrierMask> p_from, BitField<BarrierMask> p_to) {
	WARN_PRINT("Deprecated. Barriers are automatically inserted by RenderingDevice.");
}

void RenderingDevice::full_barrier() {
	WARN_PRINT("Deprecated. Barriers are automatically inserted by RenderingDevice.");
}
#endif

/*************************/
/**** TRANSFER WORKER ****/
/*************************/

static uint32_t _get_alignment_offset(uint32_t p_offset, uint32_t p_required_align) {
	uint32_t alignment_offset = (p_required_align > 0) ? (p_offset % p_required_align) : 0;
	if (alignment_offset != 0) {
		// If a particular alignment is required, add the offset as part of the required size.
		alignment_offset = p_required_align - alignment_offset;
	}

	return alignment_offset;
}

RenderingDevice::TransferWorker *RenderingDevice::_acquire_transfer_worker(uint32_t p_transfer_size, uint32_t p_required_align, uint32_t &r_staging_offset) {
	// Find the first worker that is not currently executing anything and has enough size for the transfer.
	// If no workers are available, we make a new one. If we're not allowed to make new ones, we wait until one of them is available.
	TransferWorker *transfer_worker = nullptr;
	uint32_t available_list_index = 0;
	bool transfer_worker_busy = true;
	bool transfer_worker_full = true;
	{
		MutexLock pool_lock(transfer_worker_pool_mutex);

		// If no workers are available and we've reached the max pool capacity, wait until one of them becomes available.
		bool transfer_worker_pool_full = transfer_worker_pool.size() >= transfer_worker_pool_max_size;
		while (transfer_worker_pool_available_list.is_empty() && transfer_worker_pool_full) {
			transfer_worker_pool_condition.wait(pool_lock);
		}

		// Look at all available workers first.
		for (uint32_t i = 0; i < transfer_worker_pool_available_list.size(); i++) {
			uint32_t worker_index = transfer_worker_pool_available_list[i];
			TransferWorker *candidate_worker = transfer_worker_pool[worker_index];
			candidate_worker->thread_mutex.lock();

			// Figure out if the worker can fit the transfer.
			uint32_t alignment_offset = _get_alignment_offset(candidate_worker->staging_buffer_size_in_use, p_required_align);
			uint32_t required_size = candidate_worker->staging_buffer_size_in_use + p_transfer_size + alignment_offset;
			bool candidate_worker_busy = candidate_worker->submitted;
			bool candidate_worker_full = required_size > candidate_worker->staging_buffer_size_allocated;
			bool pick_candidate = false;
			if (!candidate_worker_busy && !candidate_worker_full) {
				// A worker that can fit the transfer and is not waiting for a previous execution is the best possible candidate.
				pick_candidate = true;
			} else if (!candidate_worker_busy) {
				// The worker can't fit the transfer but it's not currently doing anything.
				// We pick it as a possible candidate if the current one is busy.
				pick_candidate = transfer_worker_busy;
			} else if (!candidate_worker_full) {
				// The worker can fit the transfer but it's currently executing previous work.
				// We pick it as a possible candidate if the current one is both busy and full.
				pick_candidate = transfer_worker_busy && transfer_worker_full;
			} else if (transfer_worker == nullptr) {
				// The worker can't fit the transfer and it's currently executing work, so it's the worst candidate.
				// We only pick if no candidate has been picked yet.
				pick_candidate = true;
			}

			if (pick_candidate) {
				if (transfer_worker != nullptr) {
					// Release the lock for the worker that was picked previously.
					transfer_worker->thread_mutex.unlock();
				}

				// Keep the lock active for this worker.
				transfer_worker = candidate_worker;
				transfer_worker_busy = candidate_worker_busy;
				transfer_worker_full = candidate_worker_full;
				available_list_index = i;

				if (!transfer_worker_busy && !transfer_worker_full) {
					// Best possible candidate, stop searching early.
					break;
				}
			} else {
				// Release the lock for the candidate.
				candidate_worker->thread_mutex.unlock();
			}
		}

		if (transfer_worker != nullptr) {
			// A worker was picked, remove it from the available list.
			transfer_worker_pool_available_list.remove_at(available_list_index);
		} else {
			DEV_ASSERT(!transfer_worker_pool_full && "A transfer worker should never be created when the pool is full.");

			// No existing worker was picked, we create a new one.
			transfer_worker = memnew(TransferWorker);
			transfer_worker->command_fence = driver->fence_create();
			transfer_worker->command_pool = driver->command_pool_create(transfer_queue_family, RDD::COMMAND_BUFFER_TYPE_PRIMARY);
			transfer_worker->command_buffer = driver->command_buffer_create(transfer_worker->command_pool);
			transfer_worker->index = transfer_worker_pool.size();
			transfer_worker_pool.push_back(transfer_worker);
			transfer_worker_operation_used_by_draw.push_back(0);
			transfer_worker->thread_mutex.lock();
		}
	}

	if (transfer_worker->submitted) {
		// Wait for the worker if the command buffer was submitted but it hasn't finished processing yet.
		_wait_for_transfer_worker(transfer_worker);
	}

	uint32_t alignment_offset = _get_alignment_offset(transfer_worker->staging_buffer_size_in_use, p_required_align);
	transfer_worker->max_transfer_size = MAX(transfer_worker->max_transfer_size, p_transfer_size);

	uint32_t required_size = transfer_worker->staging_buffer_size_in_use + p_transfer_size + alignment_offset;
	if (required_size > transfer_worker->staging_buffer_size_allocated) {
		// If there's not enough bytes to use on the staging buffer, we submit everything pending from the worker and wait for the work to be finished.
		if (transfer_worker->recording) {
			_end_transfer_worker(transfer_worker);
			_submit_transfer_worker(transfer_worker);
		}

		if (transfer_worker->submitted) {
			_wait_for_transfer_worker(transfer_worker);
		}

		alignment_offset = 0;

		// If the staging buffer can't fit the transfer, we recreate the buffer.
		const uint32_t expected_buffer_size_minimum = 16 * 1024;
		uint32_t expected_buffer_size = MAX(transfer_worker->max_transfer_size, expected_buffer_size_minimum);
		if (expected_buffer_size > transfer_worker->staging_buffer_size_allocated) {
			if (transfer_worker->staging_buffer.id != 0) {
				driver->buffer_free(transfer_worker->staging_buffer);
			}

			uint32_t new_staging_buffer_size = next_power_of_2(expected_buffer_size);
			transfer_worker->staging_buffer_size_allocated = new_staging_buffer_size;
			transfer_worker->staging_buffer = driver->buffer_create(new_staging_buffer_size, RDD::BUFFER_USAGE_TRANSFER_FROM_BIT, RDD::MEMORY_ALLOCATION_TYPE_CPU);
		}
	}

	// Add the alignment before storing the offset that will be returned.
	transfer_worker->staging_buffer_size_in_use += alignment_offset;

	// Store the offset to return and increment the current size.
	r_staging_offset = transfer_worker->staging_buffer_size_in_use;
	transfer_worker->staging_buffer_size_in_use += p_transfer_size;

	if (!transfer_worker->recording) {
		// Begin the command buffer if the worker wasn't recording yet.
		driver->command_buffer_begin(transfer_worker->command_buffer);
		transfer_worker->recording = true;
	}

	return transfer_worker;
}

void RenderingDevice::_release_transfer_worker(TransferWorker *p_transfer_worker) {
	p_transfer_worker->thread_mutex.unlock();

	transfer_worker_pool_mutex.lock();
	transfer_worker_pool_available_list.push_back(p_transfer_worker->index);
	transfer_worker_pool_mutex.unlock();
	transfer_worker_pool_condition.notify_one();
}

void RenderingDevice::_end_transfer_worker(TransferWorker *p_transfer_worker) {
	driver->command_buffer_end(p_transfer_worker->command_buffer);
	p_transfer_worker->recording = false;
}

/// 将transfer worker的命令缓冲区提交到驱动执行，并设置同步对象，保证主渲染命令在
/// 正确的时机等待这些传输操作完成。
void RenderingDevice::_submit_transfer_worker(TransferWorker *p_transfer_worker, VectorView<RDD::SemaphoreID> p_signal_semaphores) {
	driver->command_queue_execute_and_present(
		transfer_queue,						// 用于传输操作的专门队列
		{},									// 没有等待的信号量
		p_transfer_worker->command_buffer,	// 要提交的命令缓冲（拷贝/上传操作）
		p_signal_semaphores,				// 提交后要发出的信号量
		p_transfer_worker->command_fence,	// 用于同步的fence
		{}									// 不需要present
	);

	/// 将信号量加入到当前帧的等待列表中
	for (uint32_t i = 0; i < p_signal_semaphores.size(); i++) {
		// Indicate the frame should wait on these semaphores before executing the main command buffer.
		frames[frame].semaphores_to_wait_on.push_back(p_signal_semaphores[i]);
	}

	// 标记已经提交
	p_transfer_worker->submitted = true;

	{/// 记录已经提交的操作数量
		MutexLock lock(p_transfer_worker->operations_mutex);
		p_transfer_worker->operations_submitted = p_transfer_worker->operations_counter;
	}
}

void RenderingDevice::_wait_for_transfer_worker(TransferWorker *p_transfer_worker) {
	driver->fence_wait(p_transfer_worker->command_fence);
	driver->command_pool_reset(p_transfer_worker->command_pool);
	p_transfer_worker->staging_buffer_size_in_use = 0;
	p_transfer_worker->submitted = false;

	{
		MutexLock lock(p_transfer_worker->operations_mutex);
		p_transfer_worker->operations_processed = p_transfer_worker->operations_submitted;
	}

	_flush_barriers_for_transfer_worker(p_transfer_worker);
}

void RenderingDevice::_flush_barriers_for_transfer_worker(TransferWorker *p_transfer_worker) {
	// Caller must have already acquired the mutex for the worker.
	if (!p_transfer_worker->texture_barriers.is_empty()) {
		MutexLock transfer_worker_lock(transfer_worker_pool_texture_barriers_mutex);
		for (uint32_t i = 0; i < p_transfer_worker->texture_barriers.size(); i++) {
			transfer_worker_pool_texture_barriers.push_back(p_transfer_worker->texture_barriers[i]);
		}

		p_transfer_worker->texture_barriers.clear();
	}
}

void RenderingDevice::_check_transfer_worker_operation(uint32_t p_transfer_worker_index, uint64_t p_transfer_worker_operation) {
	TransferWorker *transfer_worker = transfer_worker_pool[p_transfer_worker_index];
	MutexLock lock(transfer_worker->operations_mutex);
	uint64_t &dst_operation = transfer_worker_operation_used_by_draw[transfer_worker->index];
	dst_operation = MAX(dst_operation, p_transfer_worker_operation);
}

void RenderingDevice::_check_transfer_worker_buffer(Buffer *p_buffer) {
	if (p_buffer->transfer_worker_index >= 0) {
		_check_transfer_worker_operation(p_buffer->transfer_worker_index, p_buffer->transfer_worker_operation);
		p_buffer->transfer_worker_index = -1;
	}
}

void RenderingDevice::_check_transfer_worker_texture(Texture *p_texture) {
	if (p_texture->transfer_worker_index >= 0) {
		_check_transfer_worker_operation(p_texture->transfer_worker_index, p_texture->transfer_worker_operation);
		p_texture->transfer_worker_index = -1;
	}
}

void RenderingDevice::_check_transfer_worker_vertex_array(VertexArray *p_vertex_array) {
	if (!p_vertex_array->transfer_worker_indices.is_empty()) {
		for (int i = 0; i < p_vertex_array->transfer_worker_indices.size(); i++) {
			_check_transfer_worker_operation(p_vertex_array->transfer_worker_indices[i], p_vertex_array->transfer_worker_operations[i]);
		}

		p_vertex_array->transfer_worker_indices.clear();
		p_vertex_array->transfer_worker_operations.clear();
	}
}

void RenderingDevice::_check_transfer_worker_index_array(IndexArray *p_index_array) {
	if (p_index_array->transfer_worker_index >= 0) {
		_check_transfer_worker_operation(p_index_array->transfer_worker_index, p_index_array->transfer_worker_operation);
		p_index_array->transfer_worker_index = -1;
	}
}

void RenderingDevice::_submit_transfer_workers(RDD::CommandBufferID p_draw_command_buffer) {
	MutexLock transfer_worker_lock(transfer_worker_pool_mutex);
	for (uint32_t i = 0; i < transfer_worker_pool.size(); i++) {
		TransferWorker *worker = transfer_worker_pool[i];
		if (p_draw_command_buffer) {
			MutexLock lock(worker->operations_mutex);
			if (worker->operations_processed >= transfer_worker_operation_used_by_draw[worker->index]) {
				// The operation used by the draw has already been processed, we don't need to wait on the worker.
				continue;
			}
		}

		{
			MutexLock lock(worker->thread_mutex);
			if (worker->recording) {
				VectorView<RDD::SemaphoreID> semaphores = p_draw_command_buffer ? frames[frame].transfer_worker_semaphores[i] : VectorView<RDD::SemaphoreID>();
				_end_transfer_worker(worker);
				_submit_transfer_worker(worker, semaphores);
			}

			if (p_draw_command_buffer) {
				_flush_barriers_for_transfer_worker(worker);
			}
		}
	}
}

void RenderingDevice::_submit_transfer_barriers(RDD::CommandBufferID p_draw_command_buffer) {
	MutexLock transfer_worker_lock(transfer_worker_pool_texture_barriers_mutex);
	if (!transfer_worker_pool_texture_barriers.is_empty()) {
		driver->command_pipeline_barrier(p_draw_command_buffer, RDD::PIPELINE_STAGE_COPY_BIT, RDD::PIPELINE_STAGE_ALL_COMMANDS_BIT, {}, {}, transfer_worker_pool_texture_barriers);
		transfer_worker_pool_texture_barriers.clear();
	}
}

void RenderingDevice::_wait_for_transfer_workers() {
	MutexLock transfer_worker_lock(transfer_worker_pool_mutex);
	for (TransferWorker *worker : transfer_worker_pool) {
		MutexLock lock(worker->thread_mutex);
		if (worker->submitted) {
			_wait_for_transfer_worker(worker);
		}
	}
}

void RenderingDevice::_free_transfer_workers() {
	MutexLock transfer_worker_lock(transfer_worker_pool_mutex);
	for (TransferWorker *worker : transfer_worker_pool) {
		driver->fence_free(worker->command_fence);
		driver->buffer_free(worker->staging_buffer);
		driver->command_pool_free(worker->command_pool);
		memdelete(worker);
	}

	transfer_worker_pool.clear();
}

/***********************/
/**** COMMAND GRAPH ****/
/***********************/

// 确保给定的纹理 p_texture（以及必要时它的依赖/别名）在渲染图（RDG）里“可变”（mutable），
// 即拥有一个资源使用/同步跟踪器 draw_tracker，从而能被正确地写入、设障/转场以及参与调度。
bool RenderingDevice::_texture_make_mutable(Texture *p_texture, RID p_texture_id) {
	if (p_texture->draw_tracker != nullptr) {
		// Texture already has a tracker.
		return false;
	} else {
		if (p_texture->owner.is_valid()) {
			// Texture has an owner.
			Texture *owner_texture = texture_owner.get_or_null(p_texture->owner);
			ERR_FAIL_NULL_V(owner_texture, false);

			if (owner_texture->draw_tracker != nullptr) {
				// Create a tracker for this dependency in particular.
				if (p_texture->slice_type == TEXTURE_SLICE_MAX) {
					// Shared texture.
					p_texture->draw_tracker = owner_texture->draw_tracker;
					p_texture->draw_tracker->reference_count++;
				} else {
					// Slice texture.
					HashMap<Rect2i, RDG::ResourceTracker *>::ConstIterator draw_tracker_iterator = owner_texture->slice_trackers.find(p_texture->slice_rect);
					RDG::ResourceTracker *draw_tracker = nullptr;
					if (draw_tracker_iterator != owner_texture->slice_trackers.end()) {
						// Reuse the tracker at the matching rectangle.
						draw_tracker = draw_tracker_iterator->value;
					} else {
						// Create a new tracker and store it on the map.
						draw_tracker = RDG::resource_tracker_create();
						draw_tracker->parent = owner_texture->draw_tracker;
						draw_tracker->texture_driver_id = p_texture->driver_id;
						draw_tracker->texture_size = Size2i(p_texture->width, p_texture->height);
						draw_tracker->texture_subresources = p_texture->barrier_range();
						draw_tracker->texture_usage = p_texture->usage_flags;
						draw_tracker->texture_slice_or_dirty_rect = p_texture->slice_rect;
						owner_texture->slice_trackers[p_texture->slice_rect] = draw_tracker;
					}

					p_texture->slice_trackers.clear();
					p_texture->draw_tracker = draw_tracker;
					p_texture->draw_tracker->reference_count++;
				}

				if (p_texture_id.is_valid()) {
					_dependencies_make_mutable(p_texture_id, p_texture->draw_tracker);
				}
			} else {
				// Delegate this to the owner instead, as it'll make all its dependencies mutable.
				_texture_make_mutable(owner_texture, p_texture->owner);
			}
		} else {
			// Regular texture.
			p_texture->draw_tracker = RDG::resource_tracker_create();
			p_texture->draw_tracker->texture_driver_id = p_texture->driver_id;
			p_texture->draw_tracker->texture_size = Size2i(p_texture->width, p_texture->height);
			p_texture->draw_tracker->texture_subresources = p_texture->barrier_range();
			p_texture->draw_tracker->texture_usage = p_texture->usage_flags;
			p_texture->draw_tracker->is_discardable = p_texture->is_discardable;
			p_texture->draw_tracker->reference_count = 1;

			if (p_texture_id.is_valid()) {
				if (p_texture->has_initial_data) {
					// If the texture was initialized with initial data but wasn't made mutable from the start, assume the texture sampling usage.
					p_texture->draw_tracker->usage = RDG::RESOURCE_USAGE_TEXTURE_SAMPLE;
				}

				_dependencies_make_mutable(p_texture_id, p_texture->draw_tracker);
			}
		}

		return true;
	}
}

bool RenderingDevice::_buffer_make_mutable(Buffer *p_buffer, RID p_buffer_id) {
	if (p_buffer->draw_tracker != nullptr) {
		// Buffer already has a tracker.
		return false;
	} else {
		// Create a tracker for the buffer and make all its dependencies mutable.
		p_buffer->draw_tracker = RDG::resource_tracker_create();
		p_buffer->draw_tracker->buffer_driver_id = p_buffer->driver_id;
		if (p_buffer_id.is_valid()) {
			_dependencies_make_mutable(p_buffer_id, p_buffer->draw_tracker);
		}

		return true;
	}
}

bool RenderingDevice::_vertex_array_make_mutable(VertexArray *p_vertex_array, RID p_resource_id, RDG::ResourceTracker *p_resource_tracker) {
	if (!p_vertex_array->untracked_buffers.has(p_resource_id)) {
		// Vertex array thinks the buffer is already tracked or does not use it.
		return false;
	} else {
		// Vertex array is aware of the buffer but it isn't being tracked.
		p_vertex_array->draw_trackers.push_back(p_resource_tracker);
		p_vertex_array->untracked_buffers.erase(p_resource_id);
		return true;
	}
}

bool RenderingDevice::_index_array_make_mutable(IndexArray *p_index_array, RDG::ResourceTracker *p_resource_tracker) {
	if (p_index_array->draw_tracker != nullptr) {
		// Index array already has a tracker.
		return false;
	} else {
		// Index array should assign the tracker from the buffer.
		p_index_array->draw_tracker = p_resource_tracker;
		return true;
	}
}

bool RenderingDevice::_uniform_set_make_mutable(UniformSet *p_uniform_set, RID p_resource_id, RDG::ResourceTracker *p_resource_tracker) {
	HashMap<RID, RDG::ResourceUsage>::Iterator E = p_uniform_set->untracked_usage.find(p_resource_id);
	if (!E) {
		// Uniform set thinks the resource is already tracked or does not use it.
		return false;
	} else {
		// Uniform set has seen the resource but hasn't added its tracker yet.
		p_uniform_set->draw_trackers.push_back(p_resource_tracker);
		p_uniform_set->draw_trackers_usage.push_back(E->value);
		p_uniform_set->untracked_usage.remove(E);
		return true;
	}
}

bool RenderingDevice::_dependency_make_mutable(RID p_id, RID p_resource_id, RDG::ResourceTracker *p_resource_tracker) {
	if (texture_owner.owns(p_id)) {
		Texture *texture = texture_owner.get_or_null(p_id);
		return _texture_make_mutable(texture, p_id);
	} else if (vertex_array_owner.owns(p_id)) {
		VertexArray *vertex_array = vertex_array_owner.get_or_null(p_id);
		return _vertex_array_make_mutable(vertex_array, p_resource_id, p_resource_tracker);
	} else if (index_array_owner.owns(p_id)) {
		IndexArray *index_array = index_array_owner.get_or_null(p_id);
		return _index_array_make_mutable(index_array, p_resource_tracker);
	} else if (uniform_set_owner.owns(p_id)) {
		UniformSet *uniform_set = uniform_set_owner.get_or_null(p_id);
		return _uniform_set_make_mutable(uniform_set, p_resource_id, p_resource_tracker);
	} else {
		DEV_ASSERT(false && "Unknown resource type to make mutable.");
		return false;
	}
}

bool RenderingDevice::_dependencies_make_mutable_recursive(RID p_id, RDG::ResourceTracker *p_resource_tracker) {
	bool made_mutable = false;
	HashMap<RID, HashSet<RID>>::Iterator E = dependency_map.find(p_id);
	if (E) {
		for (RID rid : E->value) {
			made_mutable = _dependency_make_mutable(rid, p_id, p_resource_tracker) || made_mutable;
		}
	}

	return made_mutable;
}

bool RenderingDevice::_dependencies_make_mutable(RID p_id, RDG::ResourceTracker *p_resource_tracker) {
	_THREAD_SAFE_METHOD_
	return _dependencies_make_mutable_recursive(p_id, p_resource_tracker);
}

/**************************/
/**** FRAME MANAGEMENT ****/
/**************************/

void RenderingDevice::free(RID p_id) {
	ERR_RENDER_THREAD_GUARD();

	_free_dependencies(p_id); // Recursively erase dependencies first, to avoid potential API problems.
	_free_internal(p_id);
}

void RenderingDevice::_free_internal(RID p_id) {
#ifdef DEV_ENABLED
	String resource_name;
	if (resource_names.has(p_id)) {
		resource_name = resource_names[p_id];
		resource_names.erase(p_id);
	}
#endif

	// Push everything so it's disposed of next time this frame index is processed (means, it's safe to do it).
	if (texture_owner.owns(p_id)) {
		Texture *texture = texture_owner.get_or_null(p_id);
		_check_transfer_worker_texture(texture);

		RDG::ResourceTracker *draw_tracker = texture->draw_tracker;
		if (draw_tracker != nullptr) {
			draw_tracker->reference_count--;
			if (draw_tracker->reference_count == 0) {
				RDG::resource_tracker_free(draw_tracker);

				if (texture->owner.is_valid() && (texture->slice_type != TEXTURE_SLICE_MAX)) {
					// If this was a texture slice, erase the tracker from the map.
					Texture *owner_texture = texture_owner.get_or_null(texture->owner);
					if (owner_texture != nullptr) {
						owner_texture->slice_trackers.erase(texture->slice_rect);
					}
				}
			}
		}

		frames[frame].textures_to_dispose_of.push_back(*texture);
		texture_owner.free(p_id);
	} else if (framebuffer_owner.owns(p_id)) {
		Framebuffer *framebuffer = framebuffer_owner.get_or_null(p_id);
		frames[frame].framebuffers_to_dispose_of.push_back(*framebuffer);

		if (framebuffer->invalidated_callback != nullptr) {
			framebuffer->invalidated_callback(framebuffer->invalidated_callback_userdata);
		}

		framebuffer_owner.free(p_id);
	} else if (sampler_owner.owns(p_id)) {
		RDD::SamplerID sampler_driver_id = *sampler_owner.get_or_null(p_id);
		frames[frame].samplers_to_dispose_of.push_back(sampler_driver_id);
		sampler_owner.free(p_id);
	} else if (vertex_buffer_owner.owns(p_id)) {
		Buffer *vertex_buffer = vertex_buffer_owner.get_or_null(p_id);
		_check_transfer_worker_buffer(vertex_buffer);

		RDG::resource_tracker_free(vertex_buffer->draw_tracker);
		frames[frame].buffers_to_dispose_of.push_back(*vertex_buffer);
		vertex_buffer_owner.free(p_id);
	} else if (vertex_array_owner.owns(p_id)) {
		vertex_array_owner.free(p_id);
	} else if (index_buffer_owner.owns(p_id)) {
		IndexBuffer *index_buffer = index_buffer_owner.get_or_null(p_id);
		_check_transfer_worker_buffer(index_buffer);

		RDG::resource_tracker_free(index_buffer->draw_tracker);
		frames[frame].buffers_to_dispose_of.push_back(*index_buffer);
		index_buffer_owner.free(p_id);
	} else if (index_array_owner.owns(p_id)) {
		index_array_owner.free(p_id);
	} else if (shader_owner.owns(p_id)) {
		Shader *shader = shader_owner.get_or_null(p_id);
		if (shader->driver_id) { // Not placeholder?
			frames[frame].shaders_to_dispose_of.push_back(*shader);
		}
		shader_owner.free(p_id);
	} else if (uniform_buffer_owner.owns(p_id)) {
		Buffer *uniform_buffer = uniform_buffer_owner.get_or_null(p_id);
		_check_transfer_worker_buffer(uniform_buffer);

		RDG::resource_tracker_free(uniform_buffer->draw_tracker);
		frames[frame].buffers_to_dispose_of.push_back(*uniform_buffer);
		uniform_buffer_owner.free(p_id);
	} else if (texture_buffer_owner.owns(p_id)) {
		Buffer *texture_buffer = texture_buffer_owner.get_or_null(p_id);
		_check_transfer_worker_buffer(texture_buffer);

		RDG::resource_tracker_free(texture_buffer->draw_tracker);
		frames[frame].buffers_to_dispose_of.push_back(*texture_buffer);
		texture_buffer_owner.free(p_id);
	} else if (storage_buffer_owner.owns(p_id)) {
		Buffer *storage_buffer = storage_buffer_owner.get_or_null(p_id);
		_check_transfer_worker_buffer(storage_buffer);

		RDG::resource_tracker_free(storage_buffer->draw_tracker);
		frames[frame].buffers_to_dispose_of.push_back(*storage_buffer);
		storage_buffer_owner.free(p_id);
	} else if (uniform_set_owner.owns(p_id)) {
		UniformSet *uniform_set = uniform_set_owner.get_or_null(p_id);
		frames[frame].uniform_sets_to_dispose_of.push_back(*uniform_set);
		uniform_set_owner.free(p_id);

		if (uniform_set->invalidated_callback != nullptr) {
			uniform_set->invalidated_callback(uniform_set->invalidated_callback_userdata);
		}
	} else if (render_pipeline_owner.owns(p_id)) {
		RenderPipeline *pipeline = render_pipeline_owner.get_or_null(p_id);
		frames[frame].render_pipelines_to_dispose_of.push_back(*pipeline);
		render_pipeline_owner.free(p_id);
	} else if (compute_pipeline_owner.owns(p_id)) {
		ComputePipeline *pipeline = compute_pipeline_owner.get_or_null(p_id);
		frames[frame].compute_pipelines_to_dispose_of.push_back(*pipeline);
		compute_pipeline_owner.free(p_id);
	} else {
#ifdef DEV_ENABLED
		ERR_PRINT("Attempted to free invalid ID: " + itos(p_id.get_id()) + " " + resource_name);
#else
		ERR_PRINT("Attempted to free invalid ID: " + itos(p_id.get_id()));
#endif
	}

	frames_pending_resources_for_processing = uint32_t(frames.size());
}

// The full list of resources that can be named is in the VkObjectType enum.
// We just expose the resources that are owned and can be accessed easily.
void RenderingDevice::set_resource_name(RID p_id, const String &p_name) {
	_THREAD_SAFE_METHOD_

	if (texture_owner.owns(p_id)) {
		Texture *texture = texture_owner.get_or_null(p_id);
		driver->set_object_name(RDD::OBJECT_TYPE_TEXTURE, texture->driver_id, p_name);
	} else if (framebuffer_owner.owns(p_id)) {
		//Framebuffer *framebuffer = framebuffer_owner.get_or_null(p_id);
		// Not implemented for now as the relationship between Framebuffer and RenderPass is very complex.
	} else if (sampler_owner.owns(p_id)) {
		RDD::SamplerID sampler_driver_id = *sampler_owner.get_or_null(p_id);
		driver->set_object_name(RDD::OBJECT_TYPE_SAMPLER, sampler_driver_id, p_name);
	} else if (vertex_buffer_owner.owns(p_id)) {
		Buffer *vertex_buffer = vertex_buffer_owner.get_or_null(p_id);
		driver->set_object_name(RDD::OBJECT_TYPE_BUFFER, vertex_buffer->driver_id, p_name);
	} else if (index_buffer_owner.owns(p_id)) {
		IndexBuffer *index_buffer = index_buffer_owner.get_or_null(p_id);
		driver->set_object_name(RDD::OBJECT_TYPE_BUFFER, index_buffer->driver_id, p_name);
	} else if (shader_owner.owns(p_id)) {
		Shader *shader = shader_owner.get_or_null(p_id);
		driver->set_object_name(RDD::OBJECT_TYPE_SHADER, shader->driver_id, p_name);
	} else if (uniform_buffer_owner.owns(p_id)) {
		Buffer *uniform_buffer = uniform_buffer_owner.get_or_null(p_id);
		driver->set_object_name(RDD::OBJECT_TYPE_BUFFER, uniform_buffer->driver_id, p_name);
	} else if (texture_buffer_owner.owns(p_id)) {
		Buffer *texture_buffer = texture_buffer_owner.get_or_null(p_id);
		driver->set_object_name(RDD::OBJECT_TYPE_BUFFER, texture_buffer->driver_id, p_name);
	} else if (storage_buffer_owner.owns(p_id)) {
		Buffer *storage_buffer = storage_buffer_owner.get_or_null(p_id);
		driver->set_object_name(RDD::OBJECT_TYPE_BUFFER, storage_buffer->driver_id, p_name);
	} else if (uniform_set_owner.owns(p_id)) {
		UniformSet *uniform_set = uniform_set_owner.get_or_null(p_id);
		driver->set_object_name(RDD::OBJECT_TYPE_UNIFORM_SET, uniform_set->driver_id, p_name);
	} else if (render_pipeline_owner.owns(p_id)) {
		RenderPipeline *pipeline = render_pipeline_owner.get_or_null(p_id);
		driver->set_object_name(RDD::OBJECT_TYPE_PIPELINE, pipeline->driver_id, p_name);
	} else if (compute_pipeline_owner.owns(p_id)) {
		ComputePipeline *pipeline = compute_pipeline_owner.get_or_null(p_id);
		driver->set_object_name(RDD::OBJECT_TYPE_PIPELINE, pipeline->driver_id, p_name);
	} else {
		ERR_PRINT("Attempted to name invalid ID: " + itos(p_id.get_id()));
		return;
	}
#ifdef DEV_ENABLED
	resource_names[p_id] = p_name;
#endif
}

void RenderingDevice::draw_command_begin_label(String p_label_name, const Color &p_color) {
	ERR_RENDER_THREAD_GUARD();

	if (!context->is_debug_utils_enabled()) {
		return;
	}

	draw_graph.begin_label(p_label_name, p_color);
}

#ifndef DISABLE_DEPRECATED
void RenderingDevice::draw_command_insert_label(String p_label_name, const Color &p_color) {
	WARN_PRINT("Deprecated. Inserting labels no longer applies due to command reordering.");
}
#endif

void RenderingDevice::draw_command_end_label() {
	ERR_RENDER_THREAD_GUARD();

	draw_graph.end_label();
}

String RenderingDevice::get_device_vendor_name() const {
	return _get_device_vendor_name(device);
}

String RenderingDevice::get_device_name() const {
	return device.name;
}

RenderingDevice::DeviceType RenderingDevice::get_device_type() const {
	return DeviceType(device.type);
}

String RenderingDevice::get_device_api_name() const {
	return driver->get_api_name();
}

bool RenderingDevice::is_composite_alpha_supported() const {
	return driver->is_composite_alpha_supported(main_queue);
}

String RenderingDevice::get_device_api_version() const {
	return driver->get_api_version();
}

String RenderingDevice::get_device_pipeline_cache_uuid() const {
	return driver->get_pipeline_cache_uuid();
}

void RenderingDevice::swap_buffers(bool p_present) {
	ERR_RENDER_THREAD_GUARD();

	_end_frame();
	_execute_frame(p_present);

	// Advance to the next frame and begin recording again.
	frame = (frame + 1) % frames.size();

	_begin_frame(true);
}

void RenderingDevice::submit() {
	ERR_RENDER_THREAD_GUARD();
	ERR_FAIL_COND_MSG(is_main_instance, "Only local devices can submit and sync.");
	ERR_FAIL_COND_MSG(local_device_processing, "device already submitted, call sync to wait until done.");

	_end_frame();
	_execute_frame(false);
	local_device_processing = true;
}

void RenderingDevice::sync() {
	ERR_RENDER_THREAD_GUARD();
	ERR_FAIL_COND_MSG(is_main_instance, "Only local devices can submit and sync.");
	ERR_FAIL_COND_MSG(!local_device_processing, "sync can only be called after a submit");

	_begin_frame(true);
	local_device_processing = false;
}

void RenderingDevice::_free_pending_resources(int p_frame) {
	// Free in dependency usage order, so nothing weird happens.
	// Pipelines.
	while (frames[p_frame].render_pipelines_to_dispose_of.front()) {
		RenderPipeline *pipeline = &frames[p_frame].render_pipelines_to_dispose_of.front()->get();

		driver->pipeline_free(pipeline->driver_id);

		frames[p_frame].render_pipelines_to_dispose_of.pop_front();
	}

	while (frames[p_frame].compute_pipelines_to_dispose_of.front()) {
		ComputePipeline *pipeline = &frames[p_frame].compute_pipelines_to_dispose_of.front()->get();

		driver->pipeline_free(pipeline->driver_id);

		frames[p_frame].compute_pipelines_to_dispose_of.pop_front();
	}

	// Uniform sets.
	while (frames[p_frame].uniform_sets_to_dispose_of.front()) {
		UniformSet *uniform_set = &frames[p_frame].uniform_sets_to_dispose_of.front()->get();

		driver->uniform_set_free(uniform_set->driver_id);

		frames[p_frame].uniform_sets_to_dispose_of.pop_front();
	}

	// Shaders.
	while (frames[p_frame].shaders_to_dispose_of.front()) {
		Shader *shader = &frames[p_frame].shaders_to_dispose_of.front()->get();

		driver->shader_free(shader->driver_id);

		frames[p_frame].shaders_to_dispose_of.pop_front();
	}

	// Samplers.
	while (frames[p_frame].samplers_to_dispose_of.front()) {
		RDD::SamplerID sampler = frames[p_frame].samplers_to_dispose_of.front()->get();

		driver->sampler_free(sampler);

		frames[p_frame].samplers_to_dispose_of.pop_front();
	}

	// Framebuffers.
	while (frames[p_frame].framebuffers_to_dispose_of.front()) {
		Framebuffer *framebuffer = &frames[p_frame].framebuffers_to_dispose_of.front()->get();
		draw_graph.framebuffer_cache_free(driver, framebuffer->framebuffer_cache);
		frames[p_frame].framebuffers_to_dispose_of.pop_front();
	}

	// Textures.
	while (frames[p_frame].textures_to_dispose_of.front()) {
		Texture *texture = &frames[p_frame].textures_to_dispose_of.front()->get();
		if (texture->bound) {
			WARN_PRINT("Deleted a texture while it was bound.");
		}

		_texture_free_shared_fallback(texture);

		texture_memory -= driver->texture_get_allocation_size(texture->driver_id);
		driver->texture_free(texture->driver_id);

		frames[p_frame].textures_to_dispose_of.pop_front();
	}

	// Buffers.
	while (frames[p_frame].buffers_to_dispose_of.front()) {
		Buffer &buffer = frames[p_frame].buffers_to_dispose_of.front()->get();
		driver->buffer_free(buffer.driver_id);
		buffer_memory -= buffer.size;

		frames[p_frame].buffers_to_dispose_of.pop_front();
	}

	if (frames_pending_resources_for_processing > 0u) {
		--frames_pending_resources_for_processing;
	}
}

uint32_t RenderingDevice::get_frame_delay() const {
	return frames.size();
}

uint64_t RenderingDevice::get_memory_usage(MemoryType p_type) const {
	switch (p_type) {
		case MEMORY_BUFFERS: {
			return buffer_memory;
		}
		case MEMORY_TEXTURES: {
			return texture_memory;
		}
		case MEMORY_TOTAL: {
			return driver->get_total_memory_used();
		}
		default: {
			DEV_ASSERT(false);
			return 0;
		}
	}
}

void RenderingDevice::_begin_frame(bool p_presented) {
	// Before writing to this frame, wait for it to be finished.
	_stall_for_frame(frame);

	if (command_pool_reset_enabled) {
		bool reset = driver->command_pool_reset(frames[frame].command_pool);
		ERR_FAIL_COND(!reset);
	}

	if (p_presented) {
		update_perf_report();
		driver->linear_uniform_set_pools_reset(frame);
	}

	// Begin recording on the frame's command buffers.
	driver->begin_segment(frame, frames_drawn++);
	driver->command_buffer_begin(frames[frame].command_buffer);

	// Reset the graph.
	draw_graph.begin();

	// Erase pending resources.
	_free_pending_resources(frame);

	// Advance staging buffers if used.
	if (upload_staging_buffers.used) {
		upload_staging_buffers.current = (upload_staging_buffers.current + 1) % upload_staging_buffers.blocks.size();
		upload_staging_buffers.used = false;
	}

	if (download_staging_buffers.used) {
		download_staging_buffers.current = (download_staging_buffers.current + 1) % download_staging_buffers.blocks.size();
		download_staging_buffers.used = false;
	}

	if (frames[frame].timestamp_count) {
		driver->timestamp_query_pool_get_results(frames[frame].timestamp_pool, frames[frame].timestamp_count, frames[frame].timestamp_result_values.ptr());
		driver->command_timestamp_query_pool_reset(frames[frame].command_buffer, frames[frame].timestamp_pool, frames[frame].timestamp_count);
		SWAP(frames[frame].timestamp_names, frames[frame].timestamp_result_names);
		SWAP(frames[frame].timestamp_cpu_values, frames[frame].timestamp_cpu_result_values);
	}

	frames[frame].timestamp_result_count = frames[frame].timestamp_count;
	frames[frame].timestamp_count = 0;
	frames[frame].index = Engine::get_singleton()->get_frames_drawn();
}

void RenderingDevice::_end_frame() {
	if (draw_list) {
		ERR_PRINT("Found open draw list at the end of the frame, this should never happen (further drawing will likely not work).");
	}

	if (compute_list) {
		ERR_PRINT("Found open compute list at the end of the frame, this should never happen (further compute will likely not work).");
	}

	// The command buffer must be copied into a stack variable as the driver workarounds can change the command buffer in use.
	RDD::CommandBufferID command_buffer = frames[frame].command_buffer;
	// 资源上传/下载类任务
	_submit_transfer_workers(command_buffer);
	// 同步/布局转换类任务
	_submit_transfer_barriers(command_buffer);

	draw_graph.end(RENDER_GRAPH_REORDER, RENDER_GRAPH_FULL_BARRIERS, command_buffer, frames[frame].command_buffer_pool);
	driver->command_buffer_end(command_buffer);
	driver->end_segment();
}

/// 说明：执行一串命令缓冲（Command Buffer），用信号量将它们串联起来，保证前后依赖。
/// 一般只有一个命令缓冲，但某些驱动（如移动端 Adreno）需要拆分成多个。
void RenderingDevice::execute_chained_cmds(bool p_present_swap_chain, RenderingDeviceDriver::FenceID p_draw_fence,
		RenderingDeviceDriver::SemaphoreID p_dst_draw_semaphore_to_signal) {
	// Execute command buffers and use semaphores to wait on the execution of the previous one.
	// Normally there's only one command buffer, but driver workarounds can force situations where
	// there'll be more.

	/// 默认只有一个命令缓冲，如果缓冲池中有记录多个要提交的，那么也记下来，然后把数据清空。
	uint32_t command_buffer_count = 1;
	RDG::CommandBufferPool &buffer_pool = frames[frame].command_buffer_pool;
	if (buffer_pool.buffers_used > 0) {
		command_buffer_count += buffer_pool.buffers_used;
		buffer_pool.buffers_used = 0;
	}

	// 清空交换链
	thread_local LocalVector<RDD::SwapChainID> swap_chains;
	swap_chains.clear();

	// Instead of having just one command; we have potentially many (which had to be split due to an
	// Adreno workaround on mobile, only if the workaround is active). Thus we must execute all of them
	// and chain them together via semaphores as dependent executions.
	// 注：可能有多个拆分的命令缓冲，需要用信号量把它们按依赖顺序串起来。
	thread_local LocalVector<RDD::SemaphoreID> wait_semaphores;
	wait_semaphores = frames[frame].semaphores_to_wait_on;

	for (uint32_t i = 0; i < command_buffer_count; i++) {
		RDD::CommandBufferID command_buffer;
		RDD::SemaphoreID signal_semaphore;
		RDD::FenceID signal_fence;
		if (i > 0) {
			command_buffer = buffer_pool.buffers[i - 1];
		} else {
			command_buffer = frames[frame].command_buffer;
		}

		if (i == (command_buffer_count - 1)) {
			// This is the last command buffer, it should signal the semaphore & fence.
			// 这是最后一个命令缓冲给你了，需要发出信号和fence
			signal_semaphore = p_dst_draw_semaphore_to_signal;
			signal_fence = p_draw_fence;

			if (p_present_swap_chain) {
				// Just present the swap chains as part of the last command execution.
				swap_chains = frames[frame].swap_chains_to_present;
			}
		} else {
			signal_semaphore = buffer_pool.semaphores[i];
			// Semaphores always need to be signaled if it's not the last command buffer.
			// 信号量总是需要被发出，如果不是最后一个命令缓冲的话。
		}

		driver->command_queue_execute_and_present(main_queue,		// 提交到主命令队列（图形队列）。
			wait_semaphores,	// 本次提交之前需要等待的信号量列表。
			command_buffer,		// 要执行的命令缓冲。
			// 这里使用一个小技巧：如果有 signal_semaphore，就把“单个信号量”作为参数；否则传空视图表示不 signal。
			signal_semaphore ? signal_semaphore : VectorView<RDD::SemaphoreID>(),
			signal_fence,		 // 若为最后一个提交则带上栅栏，否则是无效/空 ID。
			swap_chains);		 // 若需要 present，最后一个提交带上交换链；否则为空列表。

		// Make the next command buffer wait on the semaphore signaled by this one.
		/// 下一段只需等待“当前段 signal 的那个”信号量。
		wait_semaphores.resize(1);
		wait_semaphores[0] = signal_semaphore;
	}

	// 清空“本帧开始前需要等待的外部依赖”，已消费完毕。
	frames[frame].semaphores_to_wait_on.clear();
}

/// <summary>
///  执行帧，并根据参数决定是否 present 交换链。
/// </summary>
/// <param name="p_present"></param>
void RenderingDevice::_execute_frame(bool p_present) {
	// Check whether this frame should present the swap chains and in which queue.
	// 判断是否需要呈现
	const bool frame_can_present = p_present && !frames[frame].swap_chains_to_present.is_empty();
	// 判断是否是单独的呈现队列（main queue与present queue不同）
	const bool separate_present_queue = main_queue != present_queue;

	// The semaphore is required if the frame can be presented and a separate present queue is used;
	// 如果要呈现并且使用了单独的呈现队列，则需要信号量；
	// since the separate queue will wait for that semaphore before presenting.
	// 因为单独的队列会在呈现前等待该信号量。
	const RDD::SemaphoreID semaphore = (frame_can_present && separate_present_queue)
			? frames[frame].semaphore
			: RDD::SemaphoreID(nullptr);
	// 直接呈现交换链
	const bool present_swap_chain = frame_can_present && !separate_present_queue;

	// 先执行命令缓冲（这里面也会调用command_queue_execute_and_present方法，只不过是对
	// main queue调用）
	execute_chained_cmds(present_swap_chain, frames[frame].fence, semaphore);
	// Indicate the fence has been signaled so the next time the frame's contents need to be
	// used, the CPU needs to wait on the work to be completed.
	// 表明fence已经被触发，这样下次需要使用该帧内容时，CPU需要等待工作完成。
	frames[frame].fence_signaled = true;

	if (frame_can_present) {
		if (separate_present_queue) {
			// Issue the presentation separately if the presentation queue is different from the main queue.
			// 如果呈现队列与主队列不同，则单独发出呈现请求。
			driver->command_queue_execute_and_present(present_queue, frames[frame].semaphore, {}, {}, {}, frames[frame].swap_chains_to_present);
		}

		// 如果呈现队列和主队列一样，那么就意味着在执行的时候就已经呈现了。
		// 问题：会不会是在下一帧的时候呈现的？因为有一个准备屏幕的函数会先呈现一次。
		frames[frame].swap_chains_to_present.clear();
	}
}

void RenderingDevice::_stall_for_frame(uint32_t p_frame) {
	thread_local PackedByteArray packed_byte_array;

	if (frames[p_frame].fence_signaled) {
		driver->fence_wait(frames[p_frame].fence);
		frames[p_frame].fence_signaled = false;

		// Flush any pending requests for asynchronous buffer downloads.
		if (!frames[p_frame].download_buffer_get_data_requests.is_empty()) {
			for (uint32_t i = 0; i < frames[p_frame].download_buffer_get_data_requests.size(); i++) {
				const BufferGetDataRequest &request = frames[p_frame].download_buffer_get_data_requests[i];
				packed_byte_array.resize(request.size);

				uint32_t array_offset = 0;
				for (uint32_t j = 0; j < request.frame_local_count; j++) {
					uint32_t local_index = request.frame_local_index + j;
					const RDD::BufferCopyRegion &region = frames[p_frame].download_buffer_copy_regions[local_index];
					uint8_t *buffer_data = driver->buffer_map(frames[p_frame].download_buffer_staging_buffers[local_index]);
					memcpy(&packed_byte_array.write[array_offset], &buffer_data[region.dst_offset], region.size);
					driver->buffer_unmap(frames[p_frame].download_buffer_staging_buffers[local_index]);
					array_offset += region.size;
				}

				request.callback.call(packed_byte_array);
			}

			frames[p_frame].download_buffer_staging_buffers.clear();
			frames[p_frame].download_buffer_copy_regions.clear();
			frames[p_frame].download_buffer_get_data_requests.clear();
		}

		// Flush any pending requests for asynchronous texture downloads.
		if (!frames[p_frame].download_texture_get_data_requests.is_empty()) {
			uint32_t pitch_step = driver->api_trait_get(RDD::API_TRAIT_TEXTURE_DATA_ROW_PITCH_STEP);
			for (uint32_t i = 0; i < frames[p_frame].download_texture_get_data_requests.size(); i++) {
				const TextureGetDataRequest &request = frames[p_frame].download_texture_get_data_requests[i];
				uint32_t texture_size = get_image_format_required_size(request.format, request.width, request.height, request.depth, request.mipmaps);
				packed_byte_array.resize(texture_size);

				// Find the block size of the texture's format.
				uint32_t block_w = 0;
				uint32_t block_h = 0;
				get_compressed_image_format_block_dimensions(request.format, block_w, block_h);

				uint32_t block_size = get_compressed_image_format_block_byte_size(request.format);
				uint32_t pixel_size = get_image_format_pixel_size(request.format);
				uint32_t pixel_rshift = get_compressed_image_format_pixel_rshift(request.format);
				uint32_t region_size = texture_download_region_size_px;

				for (uint32_t j = 0; j < request.frame_local_count; j++) {
					uint32_t local_index = request.frame_local_index + j;
					const RDD::BufferTextureCopyRegion &region = frames[p_frame].download_buffer_texture_copy_regions[local_index];
					uint32_t w = STEPIFY(request.width >> region.texture_subresources.mipmap, block_w);
					uint32_t h = STEPIFY(request.height >> region.texture_subresources.mipmap, block_h);
					uint32_t region_w = MIN(region_size, w - region.texture_offset.x);
					uint32_t region_h = MIN(region_size, h - region.texture_offset.y);
					uint32_t region_pitch = (region_w * pixel_size * block_w) >> pixel_rshift;
					region_pitch = STEPIFY(region_pitch, pitch_step);

					uint8_t *buffer_data = driver->buffer_map(frames[p_frame].download_texture_staging_buffers[local_index]);
					const uint8_t *read_ptr = buffer_data + region.buffer_offset;
					uint8_t *write_ptr = packed_byte_array.ptrw() + frames[p_frame].download_texture_mipmap_offsets[local_index];
					uint32_t unit_size = pixel_size;
					if (block_w != 1 || block_h != 1) {
						unit_size = block_size;
					}

					write_ptr += ((region.texture_offset.y / block_h) * (w / block_w) + (region.texture_offset.x / block_w)) * unit_size;
					for (uint32_t y = region_h / block_h; y > 0; y--) {
						memcpy(write_ptr, read_ptr, (region_w / block_w) * unit_size);
						write_ptr += (w / block_w) * unit_size;
						read_ptr += region_pitch;
					}

					driver->buffer_unmap(frames[p_frame].download_texture_staging_buffers[local_index]);
				}

				request.callback.call(packed_byte_array);
			}

			frames[p_frame].download_texture_staging_buffers.clear();
			frames[p_frame].download_buffer_texture_copy_regions.clear();
			frames[p_frame].download_texture_mipmap_offsets.clear();
			frames[p_frame].download_texture_get_data_requests.clear();
		}
	}
}

void RenderingDevice::_stall_for_previous_frames() {
	for (uint32_t i = 0; i < frames.size(); i++) {
		_stall_for_frame(i);
	}
}

void RenderingDevice::_flush_and_stall_for_all_frames() {
	_stall_for_previous_frames();
	_end_frame();
	_execute_frame(false);
	_begin_frame();
}

Error RenderingDevice::initialize(RenderingContextDriver *p_context, DisplayServer::WindowID p_main_window) {
	ERR_RENDER_THREAD_GUARD_V(ERR_UNAVAILABLE);

	Error err;
	RenderingContextDriver::SurfaceID main_surface = 0;
	is_main_instance = (singleton == this) && (p_main_window != DisplayServer::INVALID_WINDOW_ID);
	if (p_main_window != DisplayServer::INVALID_WINDOW_ID) {
		// Retrieve the surface from the main window if it was specified.
		main_surface = p_context->surface_get_from_window(p_main_window);
		ERR_FAIL_COND_V(main_surface == 0, FAILED);
	}

	context = p_context;
	driver = context->driver_create();

	print_verbose("Devices:");
	int32_t device_index = Engine::get_singleton()->get_gpu_index();
	const uint32_t device_count = context->device_get_count();
	const bool detect_device = (device_index < 0) || (device_index >= int32_t(device_count));
	uint32_t device_type_score = 0;
	for (uint32_t i = 0; i < device_count; i++) {
		RenderingContextDriver::Device device_option = context->device_get(i);
		String name = device_option.name;
		String vendor = _get_device_vendor_name(device_option);
		String type = _get_device_type_name(device_option);
		bool present_supported = main_surface != 0 ? context->device_supports_present(i, main_surface) : false;
		print_verbose("  #" + itos(i) + ": " + vendor + " " + name + " - " + (present_supported ? "Supported" : "Unsupported") + ", " + type);
		if (detect_device && (present_supported || main_surface == 0)) {
			// If a window was specified, present must be supported by the device to be available as an option.
			// Assign a score for each type of device and prefer the device with the higher score.
			uint32_t option_score = _get_device_type_score(device_option);
			if (option_score > device_type_score) {
				device_index = i;
				device_type_score = option_score;
			}
		}
	}

	ERR_FAIL_COND_V_MSG((device_index < 0) || (device_index >= int32_t(device_count)), ERR_CANT_CREATE, "None of the devices supports both graphics and present queues.");

	uint32_t frame_count = 1;
	if (main_surface != 0) {
		frame_count = MAX(2U, uint32_t(GLOBAL_GET("rendering/rendering_device/vsync/frame_queue_size")));
	}

	frame = 0;
	frames.resize(frame_count);
	max_timestamp_query_elements = GLOBAL_GET("debug/settings/profiler/max_timestamp_query_elements");

	device = context->device_get(device_index);
	err = driver->initialize(device_index, frame_count);
	ERR_FAIL_COND_V_MSG(err != OK, FAILED, "Failed to initialize driver for device.");

	if (is_main_instance) {
		// Only the singleton instance with a display should print this information.
		String rendering_method;
		if (OS::get_singleton()->get_current_rendering_method() == "mobile") {
			rendering_method = "Forward Mobile";
		} else {
			rendering_method = "Forward+";
		}

		// Output our device version.
		Engine::get_singleton()->print_header(vformat("%s %s - %s - Using Device #%d: %s - %s", get_device_api_name(), get_device_api_version(), rendering_method, device_index, _get_device_vendor_name(device), device.name));
	}

	// Pick the main queue family. It is worth noting we explicitly do not request the transfer bit, as apparently the specification defines
	// that the existence of either the graphics or compute bit implies that the queue can also do transfer operations, but it is optional
	// to indicate whether it supports them or not with the dedicated transfer bit if either is set.
	BitField<RDD::CommandQueueFamilyBits> main_queue_bits;
	main_queue_bits.set_flag(RDD::COMMAND_QUEUE_FAMILY_GRAPHICS_BIT);
	main_queue_bits.set_flag(RDD::COMMAND_QUEUE_FAMILY_COMPUTE_BIT);

#if !FORCE_SEPARATE_PRESENT_QUEUE
	// Needing to use a separate queue for presentation is an edge case that remains to be seen what hardware triggers it at all.
	main_queue_family = driver->command_queue_family_get(main_queue_bits, main_surface);
	if (!main_queue_family && (main_surface != 0))
#endif
	{
		// If it was not possible to find a main queue that supports the surface, we attempt to get two different queues instead.
		main_queue_family = driver->command_queue_family_get(main_queue_bits);
		present_queue_family = driver->command_queue_family_get(BitField<RDD::CommandQueueFamilyBits>(), main_surface);
		ERR_FAIL_COND_V(!present_queue_family, FAILED);
	}

	ERR_FAIL_COND_V(!main_queue_family, FAILED);

	// Create the main queue.
	main_queue = driver->command_queue_create(main_queue_family, true);
	ERR_FAIL_COND_V(!main_queue, FAILED);

	transfer_queue_family = driver->command_queue_family_get(RDD::COMMAND_QUEUE_FAMILY_TRANSFER_BIT);
	if (transfer_queue_family) {
		// Create the transfer queue.
		transfer_queue = driver->command_queue_create(transfer_queue_family);
		ERR_FAIL_COND_V(!transfer_queue, FAILED);
	} else {
		// Use main queue as the transfer queue.
		transfer_queue = main_queue;
		transfer_queue_family = main_queue_family;
	}

	if (present_queue_family) {
		// Create the present queue.
		present_queue = driver->command_queue_create(present_queue_family);
		ERR_FAIL_COND_V(!present_queue, FAILED);
	} else {
		// Use main queue as the present queue.
		present_queue = main_queue;
		present_queue_family = main_queue_family;
	}

	// Use the processor count as the max amount of transfer workers that can be created.
	transfer_worker_pool_max_size = OS::get_singleton()->get_processor_count();

	// Create data for all the frames.
	for (uint32_t i = 0; i < frames.size(); i++) {
		frames[i].index = 0;

		// Create command pool, command buffers, semaphores and fences.
		frames[i].command_pool = driver->command_pool_create(main_queue_family, RDD::COMMAND_BUFFER_TYPE_PRIMARY);
		ERR_FAIL_COND_V(!frames[i].command_pool, FAILED);
		frames[i].command_buffer = driver->command_buffer_create(frames[i].command_pool);
		ERR_FAIL_COND_V(!frames[i].command_buffer, FAILED);
		frames[i].semaphore = driver->semaphore_create();
		ERR_FAIL_COND_V(!frames[i].semaphore, FAILED);
		frames[i].fence = driver->fence_create();
		ERR_FAIL_COND_V(!frames[i].fence, FAILED);
		frames[i].fence_signaled = false;

		// Create query pool.
		frames[i].timestamp_pool = driver->timestamp_query_pool_create(max_timestamp_query_elements);
		frames[i].timestamp_names.resize(max_timestamp_query_elements);
		frames[i].timestamp_cpu_values.resize(max_timestamp_query_elements);
		frames[i].timestamp_count = 0;
		frames[i].timestamp_result_names.resize(max_timestamp_query_elements);
		frames[i].timestamp_cpu_result_values.resize(max_timestamp_query_elements);
		frames[i].timestamp_result_values.resize(max_timestamp_query_elements);
		frames[i].timestamp_result_count = 0;

		// Assign the main queue family and command pool to the command buffer pool.
		frames[i].command_buffer_pool.pool = frames[i].command_pool;

		// Create the semaphores for the transfer workers.
		frames[i].transfer_worker_semaphores.resize(transfer_worker_pool_max_size);
		for (uint32_t j = 0; j < transfer_worker_pool_max_size; j++) {
			frames[i].transfer_worker_semaphores[j] = driver->semaphore_create();
			ERR_FAIL_COND_V(!frames[i].transfer_worker_semaphores[j], FAILED);
		}
	}

	// Start from frame count, so everything else is immediately old.
	frames_drawn = frames.size();

	// Initialize recording on the first frame.
	driver->begin_segment(frame, frames_drawn++);
	driver->command_buffer_begin(frames[0].command_buffer);

	// Create draw graph and start it initialized as well.
	draw_graph.initialize(driver, device, &_render_pass_create_from_graph, frames.size(), main_queue_family, SECONDARY_COMMAND_BUFFERS_PER_FRAME);
	draw_graph.begin();

	for (uint32_t i = 0; i < frames.size(); i++) {
		// Reset all queries in a query pool before doing any operations with them..
		driver->command_timestamp_query_pool_reset(frames[0].command_buffer, frames[i].timestamp_pool, max_timestamp_query_elements);
	}

	// Convert block size from KB.
	upload_staging_buffers.block_size = GLOBAL_GET("rendering/rendering_device/staging_buffer/block_size_kb");
	upload_staging_buffers.block_size = MAX(4u, upload_staging_buffers.block_size);
	upload_staging_buffers.block_size *= 1024;

	// Convert staging buffer size from MB.
	upload_staging_buffers.max_size = GLOBAL_GET("rendering/rendering_device/staging_buffer/max_size_mb");
	upload_staging_buffers.max_size = MAX(1u, upload_staging_buffers.max_size);
	upload_staging_buffers.max_size *= 1024 * 1024;
	upload_staging_buffers.max_size = MAX(upload_staging_buffers.max_size, upload_staging_buffers.block_size * 4);

	// Copy the sizes to the download staging buffers.
	download_staging_buffers.block_size = upload_staging_buffers.block_size;
	download_staging_buffers.max_size = upload_staging_buffers.max_size;

	texture_upload_region_size_px = GLOBAL_GET("rendering/rendering_device/staging_buffer/texture_upload_region_size_px");
	texture_upload_region_size_px = nearest_power_of_2_templated(texture_upload_region_size_px);

	texture_download_region_size_px = GLOBAL_GET("rendering/rendering_device/staging_buffer/texture_download_region_size_px");
	texture_download_region_size_px = nearest_power_of_2_templated(texture_download_region_size_px);

	// Ensure current staging block is valid and at least one per frame exists.
	upload_staging_buffers.current = 0;
	upload_staging_buffers.used = false;
	upload_staging_buffers.usage_bits = RDD::BUFFER_USAGE_TRANSFER_FROM_BIT;

	download_staging_buffers.current = 0;
	download_staging_buffers.used = false;
	download_staging_buffers.usage_bits = RDD::BUFFER_USAGE_TRANSFER_TO_BIT;

	for (uint32_t i = 0; i < frames.size(); i++) {
		// Staging was never used, create the blocks.
		err = _insert_staging_block(upload_staging_buffers);
		ERR_FAIL_COND_V(err, FAILED);

		err = _insert_staging_block(download_staging_buffers);
		ERR_FAIL_COND_V(err, FAILED);
	}

	draw_list = nullptr;
	compute_list = nullptr;

	bool project_pipeline_cache_enable = GLOBAL_GET("rendering/rendering_device/pipeline_cache/enable");
	if (is_main_instance && project_pipeline_cache_enable) {
		// Only the instance that is not a local device and is also the singleton is allowed to manage a pipeline cache.
		pipeline_cache_file_path = vformat("user://vulkan/pipelines.%s.%s",
				OS::get_singleton()->get_current_rendering_method(),
				device.name.validate_filename().replace(" ", "_").to_lower());
		if (Engine::get_singleton()->is_editor_hint()) {
			pipeline_cache_file_path += ".editor";
		}
		pipeline_cache_file_path += ".cache";

		Vector<uint8_t> cache_data = _load_pipeline_cache();
		pipeline_cache_enabled = driver->pipeline_cache_create(cache_data);
		if (pipeline_cache_enabled) {
			pipeline_cache_size = driver->pipeline_cache_query_size();
			print_verbose(vformat("Startup PSO cache (%.1f MiB)", pipeline_cache_size / (1024.0f * 1024.0f)));
		}
	}

	return OK;
}

Vector<uint8_t> RenderingDevice::_load_pipeline_cache() {
	DirAccess::make_dir_recursive_absolute(pipeline_cache_file_path.get_base_dir());

	if (FileAccess::exists(pipeline_cache_file_path)) {
		Error file_error;
		Vector<uint8_t> file_data = FileAccess::get_file_as_bytes(pipeline_cache_file_path, &file_error);
		return file_data;
	} else {
		return Vector<uint8_t>();
	}
}

void RenderingDevice::_update_pipeline_cache(bool p_closing) {
	_THREAD_SAFE_METHOD_

	{
		bool still_saving = pipeline_cache_save_task != WorkerThreadPool::INVALID_TASK_ID && !WorkerThreadPool::get_singleton()->is_task_completed(pipeline_cache_save_task);
		if (still_saving) {
			if (p_closing) {
				WorkerThreadPool::get_singleton()->wait_for_task_completion(pipeline_cache_save_task);
				pipeline_cache_save_task = WorkerThreadPool::INVALID_TASK_ID;
			} else {
				// We can't save until the currently running save is done. We'll retry next time; worst case, we'll save when exiting.
				return;
			}
		}
	}

	{
		size_t new_pipelines_cache_size = driver->pipeline_cache_query_size();
		ERR_FAIL_COND(!new_pipelines_cache_size);
		size_t difference = new_pipelines_cache_size - pipeline_cache_size;

		bool must_save = false;

		if (p_closing) {
			must_save = difference > 0;
		} else {
			float save_interval = GLOBAL_GET("rendering/rendering_device/pipeline_cache/save_chunk_size_mb");
			must_save = difference > 0 && difference / (1024.0f * 1024.0f) >= save_interval;
		}

		if (must_save) {
			pipeline_cache_size = new_pipelines_cache_size;
		} else {
			return;
		}
	}

	if (p_closing) {
		_save_pipeline_cache(this);
	} else {
		pipeline_cache_save_task = WorkerThreadPool::get_singleton()->add_native_task(&_save_pipeline_cache, this, false, "PipelineCacheSave");
	}
}

void RenderingDevice::_save_pipeline_cache(void *p_data) {
	RenderingDevice *self = static_cast<RenderingDevice *>(p_data);

	self->_thread_safe_.lock();
	Vector<uint8_t> cache_blob = self->driver->pipeline_cache_serialize();
	self->_thread_safe_.unlock();

	if (cache_blob.size() == 0) {
		return;
	}
	print_verbose(vformat("Updated PSO cache (%.1f MiB)", cache_blob.size() / (1024.0f * 1024.0f)));

	Ref<FileAccess> f = FileAccess::open(self->pipeline_cache_file_path, FileAccess::WRITE, nullptr);
	if (f.is_valid()) {
		f->store_buffer(cache_blob);
	}
}

template <typename T>
void RenderingDevice::_free_rids(T &p_owner, const char *p_type) {
	List<RID> owned;
	p_owner.get_owned_list(&owned);
	if (owned.size()) {
		if (owned.size() == 1) {
			WARN_PRINT(vformat("1 RID of type \"%s\" was leaked.", p_type));
		} else {
			WARN_PRINT(vformat("%d RIDs of type \"%s\" were leaked.", owned.size(), p_type));
		}
		for (const RID &E : owned) {
#ifdef DEV_ENABLED
			if (resource_names.has(E)) {
				print_line(String(" - ") + resource_names[E]);
			}
#endif
			free(E);
		}
	}
}

void RenderingDevice::capture_timestamp(const String &p_name) {
	ERR_RENDER_THREAD_GUARD();

	ERR_FAIL_COND_MSG(draw_list != nullptr && draw_list->state.draw_count > 0, "Capturing timestamps during draw list creation is not allowed. Offending timestamp was: " + p_name);
	ERR_FAIL_COND_MSG(compute_list != nullptr && compute_list->state.dispatch_count > 0, "Capturing timestamps during compute list creation is not allowed. Offending timestamp was: " + p_name);
	ERR_FAIL_COND_MSG(frames[frame].timestamp_count >= max_timestamp_query_elements, vformat("Tried capturing more timestamps than the configured maximum (%d). You can increase this limit in the project settings under 'Debug/Settings' called 'Max Timestamp Query Elements'.", max_timestamp_query_elements));

	draw_graph.add_capture_timestamp(frames[frame].timestamp_pool, frames[frame].timestamp_count);

	frames[frame].timestamp_names[frames[frame].timestamp_count] = p_name;
	frames[frame].timestamp_cpu_values[frames[frame].timestamp_count] = OS::get_singleton()->get_ticks_usec();
	frames[frame].timestamp_count++;
}

uint64_t RenderingDevice::get_driver_resource(DriverResource p_resource, RID p_rid, uint64_t p_index) {
	ERR_RENDER_THREAD_GUARD_V(0);

	uint64_t driver_id = 0;
	switch (p_resource) {
		case DRIVER_RESOURCE_LOGICAL_DEVICE:
		case DRIVER_RESOURCE_PHYSICAL_DEVICE:
		case DRIVER_RESOURCE_TOPMOST_OBJECT:
			break;
		case DRIVER_RESOURCE_COMMAND_QUEUE:
			driver_id = main_queue.id;
			break;
		case DRIVER_RESOURCE_QUEUE_FAMILY:
			driver_id = main_queue_family.id;
			break;
		case DRIVER_RESOURCE_TEXTURE:
		case DRIVER_RESOURCE_TEXTURE_VIEW:
		case DRIVER_RESOURCE_TEXTURE_DATA_FORMAT: {
			Texture *tex = texture_owner.get_or_null(p_rid);
			ERR_FAIL_NULL_V(tex, 0);

			driver_id = tex->driver_id.id;
		} break;
		case DRIVER_RESOURCE_SAMPLER: {
			RDD::SamplerID *sampler_driver_id = sampler_owner.get_or_null(p_rid);
			ERR_FAIL_NULL_V(sampler_driver_id, 0);

			driver_id = (*sampler_driver_id).id;
		} break;
		case DRIVER_RESOURCE_UNIFORM_SET: {
			UniformSet *uniform_set = uniform_set_owner.get_or_null(p_rid);
			ERR_FAIL_NULL_V(uniform_set, 0);

			driver_id = uniform_set->driver_id.id;
		} break;
		case DRIVER_RESOURCE_BUFFER: {
			Buffer *buffer = nullptr;
			if (vertex_buffer_owner.owns(p_rid)) {
				buffer = vertex_buffer_owner.get_or_null(p_rid);
			} else if (index_buffer_owner.owns(p_rid)) {
				buffer = index_buffer_owner.get_or_null(p_rid);
			} else if (uniform_buffer_owner.owns(p_rid)) {
				buffer = uniform_buffer_owner.get_or_null(p_rid);
			} else if (texture_buffer_owner.owns(p_rid)) {
				buffer = texture_buffer_owner.get_or_null(p_rid);
			} else if (storage_buffer_owner.owns(p_rid)) {
				buffer = storage_buffer_owner.get_or_null(p_rid);
			}
			ERR_FAIL_NULL_V(buffer, 0);

			driver_id = buffer->driver_id.id;
		} break;
		case DRIVER_RESOURCE_COMPUTE_PIPELINE: {
			ComputePipeline *compute_pipeline = compute_pipeline_owner.get_or_null(p_rid);
			ERR_FAIL_NULL_V(compute_pipeline, 0);

			driver_id = compute_pipeline->driver_id.id;
		} break;
		case DRIVER_RESOURCE_RENDER_PIPELINE: {
			RenderPipeline *render_pipeline = render_pipeline_owner.get_or_null(p_rid);
			ERR_FAIL_NULL_V(render_pipeline, 0);

			driver_id = render_pipeline->driver_id.id;
		} break;
		default: {
			ERR_FAIL_V(0);
		} break;
	}

	return driver->get_resource_native_handle(p_resource, driver_id);
}

String RenderingDevice::get_driver_and_device_memory_report() const {
	return context->get_driver_and_device_memory_report();
}

String RenderingDevice::get_tracked_object_name(uint32_t p_type_index) const {
	return context->get_tracked_object_name(p_type_index);
}

uint64_t RenderingDevice::get_tracked_object_type_count() const {
	return context->get_tracked_object_type_count();
}

uint64_t RenderingDevice::get_driver_total_memory() const {
	return context->get_driver_total_memory();
}

uint64_t RenderingDevice::get_driver_allocation_count() const {
	return context->get_driver_allocation_count();
}

uint64_t RenderingDevice::get_driver_memory_by_object_type(uint32_t p_type) const {
	return context->get_driver_memory_by_object_type(p_type);
}

uint64_t RenderingDevice::get_driver_allocs_by_object_type(uint32_t p_type) const {
	return context->get_driver_allocs_by_object_type(p_type);
}

uint64_t RenderingDevice::get_device_total_memory() const {
	return context->get_device_total_memory();
}

uint64_t RenderingDevice::get_device_allocation_count() const {
	return context->get_device_allocation_count();
}

uint64_t RenderingDevice::get_device_memory_by_object_type(uint32_t type) const {
	return context->get_device_memory_by_object_type(type);
}

uint64_t RenderingDevice::get_device_allocs_by_object_type(uint32_t type) const {
	return context->get_device_allocs_by_object_type(type);
}

uint32_t RenderingDevice::get_captured_timestamps_count() const {
	ERR_RENDER_THREAD_GUARD_V(0);
	return frames[frame].timestamp_result_count;
}

uint64_t RenderingDevice::get_captured_timestamps_frame() const {
	ERR_RENDER_THREAD_GUARD_V(0);
	return frames[frame].index;
}

uint64_t RenderingDevice::get_captured_timestamp_gpu_time(uint32_t p_index) const {
	ERR_RENDER_THREAD_GUARD_V(0);
	ERR_FAIL_UNSIGNED_INDEX_V(p_index, frames[frame].timestamp_result_count, 0);
	return driver->timestamp_query_result_to_time(frames[frame].timestamp_result_values[p_index]);
}

uint64_t RenderingDevice::get_captured_timestamp_cpu_time(uint32_t p_index) const {
	ERR_RENDER_THREAD_GUARD_V(0);
	ERR_FAIL_UNSIGNED_INDEX_V(p_index, frames[frame].timestamp_result_count, 0);
	return frames[frame].timestamp_cpu_result_values[p_index];
}

String RenderingDevice::get_captured_timestamp_name(uint32_t p_index) const {
	ERR_FAIL_UNSIGNED_INDEX_V(p_index, frames[frame].timestamp_result_count, String());
	return frames[frame].timestamp_result_names[p_index];
}

uint64_t RenderingDevice::limit_get(Limit p_limit) const {
	return driver->limit_get(p_limit);
}

void RenderingDevice::finalize() {
	ERR_RENDER_THREAD_GUARD();

	if (!frames.is_empty()) {
		// Wait for all frames to have finished rendering.
		_flush_and_stall_for_all_frames();
	}

	// Wait for transfer workers to finish.
	_submit_transfer_workers();
	_wait_for_transfer_workers();

	// Delete everything the graph has created.
	draw_graph.finalize();

	// Free all resources.
	_free_rids(render_pipeline_owner, "Pipeline");
	_free_rids(compute_pipeline_owner, "Compute");
	_free_rids(uniform_set_owner, "UniformSet");
	_free_rids(texture_buffer_owner, "TextureBuffer");
	_free_rids(storage_buffer_owner, "StorageBuffer");
	_free_rids(uniform_buffer_owner, "UniformBuffer");
	_free_rids(shader_owner, "Shader");
	_free_rids(index_array_owner, "IndexArray");
	_free_rids(index_buffer_owner, "IndexBuffer");
	_free_rids(vertex_array_owner, "VertexArray");
	_free_rids(vertex_buffer_owner, "VertexBuffer");
	_free_rids(framebuffer_owner, "Framebuffer");
	_free_rids(sampler_owner, "Sampler");
	{
		// For textures it's a bit more difficult because they may be shared.
		List<RID> owned;
		texture_owner.get_owned_list(&owned);
		if (owned.size()) {
			if (owned.size() == 1) {
				WARN_PRINT("1 RID of type \"Texture\" was leaked.");
			} else {
				WARN_PRINT(vformat("%d RIDs of type \"Texture\" were leaked.", owned.size()));
			}
			// Free shared first.
			for (List<RID>::Element *E = owned.front(); E;) {
				List<RID>::Element *N = E->next();
				if (texture_is_shared(E->get())) {
#ifdef DEV_ENABLED
					if (resource_names.has(E->get())) {
						print_line(String(" - ") + resource_names[E->get()]);
					}
#endif
					free(E->get());
					owned.erase(E);
				}
				E = N;
			}
			// Free non shared second, this will avoid an error trying to free unexisting textures due to dependencies.
			for (const RID &E : owned) {
#ifdef DEV_ENABLED
				if (resource_names.has(E)) {
					print_line(String(" - ") + resource_names[E]);
				}
#endif
				free(E);
			}
		}
	}

	// Erase the transfer workers after all resources have been freed.
	_free_transfer_workers();

	// Free everything pending.
	for (uint32_t i = 0; i < frames.size(); i++) {
		int f = (frame + i) % frames.size();
		_free_pending_resources(f);
		driver->command_pool_free(frames[i].command_pool);
		driver->timestamp_query_pool_free(frames[i].timestamp_pool);
		driver->semaphore_free(frames[i].semaphore);
		driver->fence_free(frames[i].fence);

		RDG::CommandBufferPool &buffer_pool = frames[i].command_buffer_pool;
		for (uint32_t j = 0; j < buffer_pool.buffers.size(); j++) {
			driver->semaphore_free(buffer_pool.semaphores[j]);
		}

		for (uint32_t j = 0; j < frames[i].transfer_worker_semaphores.size(); j++) {
			driver->semaphore_free(frames[i].transfer_worker_semaphores[j]);
		}
	}

	if (pipeline_cache_enabled) {
		_update_pipeline_cache(true);
		driver->pipeline_cache_free();
	}

	frames.clear();

	for (int i = 0; i < upload_staging_buffers.blocks.size(); i++) {
		driver->buffer_free(upload_staging_buffers.blocks[i].driver_id);
	}

	for (int i = 0; i < download_staging_buffers.blocks.size(); i++) {
		driver->buffer_free(download_staging_buffers.blocks[i].driver_id);
	}

	while (vertex_formats.size()) {
		HashMap<VertexFormatID, VertexDescriptionCache>::Iterator temp = vertex_formats.begin();
		driver->vertex_format_free(temp->value.driver_id);
		vertex_formats.remove(temp);
	}

	for (KeyValue<FramebufferFormatID, FramebufferFormat> &E : framebuffer_formats) {
		driver->render_pass_free(E.value.render_pass);
	}
	framebuffer_formats.clear();

	// Delete the swap chains created for the screens.
	for (const KeyValue<DisplayServer::WindowID, RDD::SwapChainID> &it : screen_swap_chains) {
		driver->swap_chain_free(it.value);
	}

	screen_swap_chains.clear();

	// Delete the command queues.
	if (present_queue) {
		if (main_queue != present_queue) {
			// Only delete the present queue if it's unique.
			driver->command_queue_free(present_queue);
		}

		present_queue = RDD::CommandQueueID();
	}

	if (transfer_queue) {
		if (main_queue != transfer_queue) {
			// Only delete the transfer queue if it's unique.
			driver->command_queue_free(transfer_queue);
		}

		transfer_queue = RDD::CommandQueueID();
	}

	if (main_queue) {
		driver->command_queue_free(main_queue);
		main_queue = RDD::CommandQueueID();
	}

	// Delete the driver once everything else has been deleted.
	if (driver != nullptr) {
		context->driver_free(driver);
		driver = nullptr;
	}

	// All these should be clear at this point.
	ERR_FAIL_COND(dependency_map.size());
	ERR_FAIL_COND(reverse_dependency_map.size());
}

void RenderingDevice::_set_max_fps(int p_max_fps) {
	for (const KeyValue<DisplayServer::WindowID, RDD::SwapChainID> &it : screen_swap_chains) {
		driver->swap_chain_set_max_fps(it.value, p_max_fps);
	}
}

RenderingDevice *RenderingDevice::create_local_device() {
	RenderingDevice *rd = memnew(RenderingDevice);
	rd->initialize(context);
	return rd;
}

bool RenderingDevice::has_feature(const Features p_feature) const {
	return driver->has_feature(p_feature);
}

void RenderingDevice::_bind_methods() {
	ClassDB::bind_method(D_METHOD("texture_create", "format", "view", "data"), &RenderingDevice::_texture_create, DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("texture_create_shared", "view", "with_texture"), &RenderingDevice::_texture_create_shared);
	ClassDB::bind_method(D_METHOD("texture_create_shared_from_slice", "view", "with_texture", "layer", "mipmap", "mipmaps", "slice_type"), &RenderingDevice::_texture_create_shared_from_slice, DEFVAL(1), DEFVAL(TEXTURE_SLICE_2D));
	ClassDB::bind_method(D_METHOD("texture_create_from_extension", "type", "format", "samples", "usage_flags", "image", "width", "height", "depth", "layers"), &RenderingDevice::texture_create_from_extension);

	ClassDB::bind_method(D_METHOD("texture_update", "texture", "layer", "data"), &RenderingDevice::texture_update);
	ClassDB::bind_method(D_METHOD("texture_get_data", "texture", "layer"), &RenderingDevice::texture_get_data);
	ClassDB::bind_method(D_METHOD("texture_get_data_async", "texture", "layer", "callback"), &RenderingDevice::texture_get_data_async);

	ClassDB::bind_method(D_METHOD("texture_is_format_supported_for_usage", "format", "usage_flags"), &RenderingDevice::texture_is_format_supported_for_usage);

	ClassDB::bind_method(D_METHOD("texture_is_shared", "texture"), &RenderingDevice::texture_is_shared);
	ClassDB::bind_method(D_METHOD("texture_is_valid", "texture"), &RenderingDevice::texture_is_valid);

	ClassDB::bind_method(D_METHOD("texture_set_discardable", "texture", "discardable"), &RenderingDevice::texture_set_discardable);
	ClassDB::bind_method(D_METHOD("texture_is_discardable", "texture"), &RenderingDevice::texture_is_discardable);

	ClassDB::bind_method(D_METHOD("texture_copy", "from_texture", "to_texture", "from_pos", "to_pos", "size", "src_mipmap", "dst_mipmap", "src_layer", "dst_layer"), &RenderingDevice::texture_copy);
	ClassDB::bind_method(D_METHOD("texture_clear", "texture", "color", "base_mipmap", "mipmap_count", "base_layer", "layer_count"), &RenderingDevice::texture_clear);
	ClassDB::bind_method(D_METHOD("texture_resolve_multisample", "from_texture", "to_texture"), &RenderingDevice::texture_resolve_multisample);

	ClassDB::bind_method(D_METHOD("texture_get_format", "texture"), &RenderingDevice::_texture_get_format);
#ifndef DISABLE_DEPRECATED
	ClassDB::bind_method(D_METHOD("texture_get_native_handle", "texture"), &RenderingDevice::texture_get_native_handle);
#endif

	ClassDB::bind_method(D_METHOD("framebuffer_format_create", "attachments", "view_count"), &RenderingDevice::_framebuffer_format_create, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("framebuffer_format_create_multipass", "attachments", "passes", "view_count"), &RenderingDevice::_framebuffer_format_create_multipass, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("framebuffer_format_create_empty", "samples"), &RenderingDevice::framebuffer_format_create_empty, DEFVAL(TEXTURE_SAMPLES_1));
	ClassDB::bind_method(D_METHOD("framebuffer_format_get_texture_samples", "format", "render_pass"), &RenderingDevice::framebuffer_format_get_texture_samples, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("framebuffer_create", "textures", "validate_with_format", "view_count"), &RenderingDevice::_framebuffer_create, DEFVAL(INVALID_FORMAT_ID), DEFVAL(1));
	ClassDB::bind_method(D_METHOD("framebuffer_create_multipass", "textures", "passes", "validate_with_format", "view_count"), &RenderingDevice::_framebuffer_create_multipass, DEFVAL(INVALID_FORMAT_ID), DEFVAL(1));
	ClassDB::bind_method(D_METHOD("framebuffer_create_empty", "size", "samples", "validate_with_format"), &RenderingDevice::framebuffer_create_empty, DEFVAL(TEXTURE_SAMPLES_1), DEFVAL(INVALID_FORMAT_ID));
	ClassDB::bind_method(D_METHOD("framebuffer_get_format", "framebuffer"), &RenderingDevice::framebuffer_get_format);
	ClassDB::bind_method(D_METHOD("framebuffer_is_valid", "framebuffer"), &RenderingDevice::framebuffer_is_valid);

	ClassDB::bind_method(D_METHOD("sampler_create", "state"), &RenderingDevice::_sampler_create);
	ClassDB::bind_method(D_METHOD("sampler_is_format_supported_for_filter", "format", "sampler_filter"), &RenderingDevice::sampler_is_format_supported_for_filter);

	ClassDB::bind_method(D_METHOD("vertex_buffer_create", "size_bytes", "data", "use_as_storage"), &RenderingDevice::vertex_buffer_create, DEFVAL(Vector<uint8_t>()), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("vertex_format_create", "vertex_descriptions"), &RenderingDevice::_vertex_format_create);
	ClassDB::bind_method(D_METHOD("vertex_array_create", "vertex_count", "vertex_format", "src_buffers", "offsets"), &RenderingDevice::_vertex_array_create, DEFVAL(Vector<int64_t>()));

	ClassDB::bind_method(D_METHOD("index_buffer_create", "size_indices", "format", "data", "use_restart_indices"), &RenderingDevice::index_buffer_create, DEFVAL(Vector<uint8_t>()), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("index_array_create", "index_buffer", "index_offset", "index_count"), &RenderingDevice::index_array_create);

	ClassDB::bind_method(D_METHOD("shader_compile_spirv_from_source", "shader_source", "allow_cache"), &RenderingDevice::_shader_compile_spirv_from_source, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("shader_compile_binary_from_spirv", "spirv_data", "name"), &RenderingDevice::_shader_compile_binary_from_spirv, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("shader_create_from_spirv", "spirv_data", "name"), &RenderingDevice::_shader_create_from_spirv, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("shader_create_from_bytecode", "binary_data", "placeholder_rid"), &RenderingDevice::shader_create_from_bytecode, DEFVAL(RID()));
	ClassDB::bind_method(D_METHOD("shader_create_placeholder"), &RenderingDevice::shader_create_placeholder);

	ClassDB::bind_method(D_METHOD("shader_get_vertex_input_attribute_mask", "shader"), &RenderingDevice::shader_get_vertex_input_attribute_mask);

	ClassDB::bind_method(D_METHOD("uniform_buffer_create", "size_bytes", "data"), &RenderingDevice::uniform_buffer_create, DEFVAL(Vector<uint8_t>()));
	ClassDB::bind_method(D_METHOD("storage_buffer_create", "size_bytes", "data", "usage"), &RenderingDevice::storage_buffer_create, DEFVAL(Vector<uint8_t>()), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("texture_buffer_create", "size_bytes", "format", "data"), &RenderingDevice::texture_buffer_create, DEFVAL(Vector<uint8_t>()));

	ClassDB::bind_method(D_METHOD("uniform_set_create", "uniforms", "shader", "shader_set"), &RenderingDevice::_uniform_set_create);
	ClassDB::bind_method(D_METHOD("uniform_set_is_valid", "uniform_set"), &RenderingDevice::uniform_set_is_valid);

	ClassDB::bind_method(D_METHOD("buffer_copy", "src_buffer", "dst_buffer", "src_offset", "dst_offset", "size"), &RenderingDevice::buffer_copy);
	ClassDB::bind_method(D_METHOD("buffer_update", "buffer", "offset", "size_bytes", "data"), &RenderingDevice::_buffer_update_bind);
	ClassDB::bind_method(D_METHOD("buffer_clear", "buffer", "offset", "size_bytes"), &RenderingDevice::buffer_clear);
	ClassDB::bind_method(D_METHOD("buffer_get_data", "buffer", "offset_bytes", "size_bytes"), &RenderingDevice::buffer_get_data, DEFVAL(0), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("buffer_get_data_async", "buffer", "callback", "offset_bytes", "size_bytes"), &RenderingDevice::buffer_get_data_async, DEFVAL(0), DEFVAL(0));

	ClassDB::bind_method(D_METHOD("render_pipeline_create", "shader", "framebuffer_format", "vertex_format", "primitive", "rasterization_state", "multisample_state", "stencil_state", "color_blend_state", "dynamic_state_flags", "for_render_pass", "specialization_constants"), &RenderingDevice::_render_pipeline_create, DEFVAL(0), DEFVAL(0), DEFVAL(TypedArray<RDPipelineSpecializationConstant>()));
	ClassDB::bind_method(D_METHOD("render_pipeline_is_valid", "render_pipeline"), &RenderingDevice::render_pipeline_is_valid);

	ClassDB::bind_method(D_METHOD("compute_pipeline_create", "shader", "specialization_constants"), &RenderingDevice::_compute_pipeline_create, DEFVAL(TypedArray<RDPipelineSpecializationConstant>()));
	ClassDB::bind_method(D_METHOD("compute_pipeline_is_valid", "compute_pipeline"), &RenderingDevice::compute_pipeline_is_valid);

	ClassDB::bind_method(D_METHOD("screen_get_width", "screen"), &RenderingDevice::screen_get_width, DEFVAL(DisplayServer::MAIN_WINDOW_ID));
	ClassDB::bind_method(D_METHOD("screen_get_height", "screen"), &RenderingDevice::screen_get_height, DEFVAL(DisplayServer::MAIN_WINDOW_ID));
	ClassDB::bind_method(D_METHOD("screen_get_framebuffer_format", "screen"), &RenderingDevice::screen_get_framebuffer_format, DEFVAL(DisplayServer::MAIN_WINDOW_ID));

	ClassDB::bind_method(D_METHOD("draw_list_begin_for_screen", "screen", "clear_color"), &RenderingDevice::draw_list_begin_for_screen, DEFVAL(DisplayServer::MAIN_WINDOW_ID), DEFVAL(Color()));

	ClassDB::bind_method(D_METHOD("draw_list_begin", "framebuffer", "draw_flags", "clear_color_values", "clear_depth_value", "clear_stencil_value", "region", "breadcrumb"), &RenderingDevice::draw_list_begin, DEFVAL(DRAW_DEFAULT_ALL), DEFVAL(Vector<Color>()), DEFVAL(1.0), DEFVAL(0), DEFVAL(Rect2()), DEFVAL(0));
#ifndef DISABLE_DEPRECATED
	ClassDB::bind_method(D_METHOD("draw_list_begin_split", "framebuffer", "splits", "initial_color_action", "final_color_action", "initial_depth_action", "final_depth_action", "clear_color_values", "clear_depth", "clear_stencil", "region", "storage_textures"), &RenderingDevice::_draw_list_begin_split, DEFVAL(Vector<Color>()), DEFVAL(1.0), DEFVAL(0), DEFVAL(Rect2()), DEFVAL(TypedArray<RID>()));
#endif

	ClassDB::bind_method(D_METHOD("draw_list_set_blend_constants", "draw_list", "color"), &RenderingDevice::draw_list_set_blend_constants);
	ClassDB::bind_method(D_METHOD("draw_list_bind_render_pipeline", "draw_list", "render_pipeline"), &RenderingDevice::draw_list_bind_render_pipeline);
	ClassDB::bind_method(D_METHOD("draw_list_bind_uniform_set", "draw_list", "uniform_set", "set_index"), &RenderingDevice::draw_list_bind_uniform_set);
	ClassDB::bind_method(D_METHOD("draw_list_bind_vertex_array", "draw_list", "vertex_array"), &RenderingDevice::draw_list_bind_vertex_array);
	ClassDB::bind_method(D_METHOD("draw_list_bind_index_array", "draw_list", "index_array"), &RenderingDevice::draw_list_bind_index_array);
	ClassDB::bind_method(D_METHOD("draw_list_set_push_constant", "draw_list", "buffer", "size_bytes"), &RenderingDevice::_draw_list_set_push_constant);

	ClassDB::bind_method(D_METHOD("draw_list_draw", "draw_list", "use_indices", "instances", "procedural_vertex_count"), &RenderingDevice::draw_list_draw, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("draw_list_draw_indirect", "draw_list", "use_indices", "buffer", "offset", "draw_count", "stride"), &RenderingDevice::draw_list_draw_indirect, DEFVAL(0), DEFVAL(1), DEFVAL(0));

	ClassDB::bind_method(D_METHOD("draw_list_enable_scissor", "draw_list", "rect"), &RenderingDevice::draw_list_enable_scissor, DEFVAL(Rect2()));
	ClassDB::bind_method(D_METHOD("draw_list_disable_scissor", "draw_list"), &RenderingDevice::draw_list_disable_scissor);

	ClassDB::bind_method(D_METHOD("draw_list_switch_to_next_pass"), &RenderingDevice::draw_list_switch_to_next_pass);
#ifndef DISABLE_DEPRECATED
	ClassDB::bind_method(D_METHOD("draw_list_switch_to_next_pass_split", "splits"), &RenderingDevice::_draw_list_switch_to_next_pass_split);
#endif

	ClassDB::bind_method(D_METHOD("draw_list_end"), &RenderingDevice::draw_list_end);

	ClassDB::bind_method(D_METHOD("compute_list_begin"), &RenderingDevice::compute_list_begin);
	ClassDB::bind_method(D_METHOD("compute_list_bind_compute_pipeline", "compute_list", "compute_pipeline"), &RenderingDevice::compute_list_bind_compute_pipeline);
	ClassDB::bind_method(D_METHOD("compute_list_set_push_constant", "compute_list", "buffer", "size_bytes"), &RenderingDevice::_compute_list_set_push_constant);
	ClassDB::bind_method(D_METHOD("compute_list_bind_uniform_set", "compute_list", "uniform_set", "set_index"), &RenderingDevice::compute_list_bind_uniform_set);
	ClassDB::bind_method(D_METHOD("compute_list_dispatch", "compute_list", "x_groups", "y_groups", "z_groups"), &RenderingDevice::compute_list_dispatch);
	ClassDB::bind_method(D_METHOD("compute_list_dispatch_indirect", "compute_list", "buffer", "offset"), &RenderingDevice::compute_list_dispatch_indirect);
	ClassDB::bind_method(D_METHOD("compute_list_add_barrier", "compute_list"), &RenderingDevice::compute_list_add_barrier);
	ClassDB::bind_method(D_METHOD("compute_list_end"), &RenderingDevice::compute_list_end);

	ClassDB::bind_method(D_METHOD("free_rid", "rid"), &RenderingDevice::free);

	ClassDB::bind_method(D_METHOD("capture_timestamp", "name"), &RenderingDevice::capture_timestamp);
	ClassDB::bind_method(D_METHOD("get_captured_timestamps_count"), &RenderingDevice::get_captured_timestamps_count);
	ClassDB::bind_method(D_METHOD("get_captured_timestamps_frame"), &RenderingDevice::get_captured_timestamps_frame);
	ClassDB::bind_method(D_METHOD("get_captured_timestamp_gpu_time", "index"), &RenderingDevice::get_captured_timestamp_gpu_time);
	ClassDB::bind_method(D_METHOD("get_captured_timestamp_cpu_time", "index"), &RenderingDevice::get_captured_timestamp_cpu_time);
	ClassDB::bind_method(D_METHOD("get_captured_timestamp_name", "index"), &RenderingDevice::get_captured_timestamp_name);

	ClassDB::bind_method(D_METHOD("limit_get", "limit"), &RenderingDevice::limit_get);
	ClassDB::bind_method(D_METHOD("get_frame_delay"), &RenderingDevice::get_frame_delay);
	ClassDB::bind_method(D_METHOD("submit"), &RenderingDevice::submit);
	ClassDB::bind_method(D_METHOD("sync"), &RenderingDevice::sync);

#ifndef DISABLE_DEPRECATED
	ClassDB::bind_method(D_METHOD("barrier", "from", "to"), &RenderingDevice::barrier, DEFVAL(BARRIER_MASK_ALL_BARRIERS), DEFVAL(BARRIER_MASK_ALL_BARRIERS));
	ClassDB::bind_method(D_METHOD("full_barrier"), &RenderingDevice::full_barrier);
#endif

	ClassDB::bind_method(D_METHOD("create_local_device"), &RenderingDevice::create_local_device);

	ClassDB::bind_method(D_METHOD("set_resource_name", "id", "name"), &RenderingDevice::set_resource_name);

	ClassDB::bind_method(D_METHOD("draw_command_begin_label", "name", "color"), &RenderingDevice::draw_command_begin_label);
#ifndef DISABLE_DEPRECATED
	ClassDB::bind_method(D_METHOD("draw_command_insert_label", "name", "color"), &RenderingDevice::draw_command_insert_label);
#endif
	ClassDB::bind_method(D_METHOD("draw_command_end_label"), &RenderingDevice::draw_command_end_label);

	ClassDB::bind_method(D_METHOD("get_device_vendor_name"), &RenderingDevice::get_device_vendor_name);
	ClassDB::bind_method(D_METHOD("get_device_name"), &RenderingDevice::get_device_name);
	ClassDB::bind_method(D_METHOD("get_device_pipeline_cache_uuid"), &RenderingDevice::get_device_pipeline_cache_uuid);

	ClassDB::bind_method(D_METHOD("get_memory_usage", "type"), &RenderingDevice::get_memory_usage);

	ClassDB::bind_method(D_METHOD("get_driver_resource", "resource", "rid", "index"), &RenderingDevice::get_driver_resource);

	ClassDB::bind_method(D_METHOD("get_perf_report"), &RenderingDevice::get_perf_report);

	ClassDB::bind_method(D_METHOD("get_driver_and_device_memory_report"), &RenderingDevice::get_driver_and_device_memory_report);
	ClassDB::bind_method(D_METHOD("get_tracked_object_name", "type_index"), &RenderingDevice::get_tracked_object_name);
	ClassDB::bind_method(D_METHOD("get_tracked_object_type_count"), &RenderingDevice::get_tracked_object_type_count);
	ClassDB::bind_method(D_METHOD("get_driver_total_memory"), &RenderingDevice::get_driver_total_memory);
	ClassDB::bind_method(D_METHOD("get_driver_allocation_count"), &RenderingDevice::get_driver_allocation_count);
	ClassDB::bind_method(D_METHOD("get_driver_memory_by_object_type", "type"), &RenderingDevice::get_driver_memory_by_object_type);
	ClassDB::bind_method(D_METHOD("get_driver_allocs_by_object_type", "type"), &RenderingDevice::get_driver_allocs_by_object_type);
	ClassDB::bind_method(D_METHOD("get_device_total_memory"), &RenderingDevice::get_device_total_memory);
	ClassDB::bind_method(D_METHOD("get_device_allocation_count"), &RenderingDevice::get_device_allocation_count);
	ClassDB::bind_method(D_METHOD("get_device_memory_by_object_type", "type"), &RenderingDevice::get_device_memory_by_object_type);
	ClassDB::bind_method(D_METHOD("get_device_allocs_by_object_type", "type"), &RenderingDevice::get_device_allocs_by_object_type);

	BIND_ENUM_CONSTANT(DEVICE_TYPE_OTHER);
	BIND_ENUM_CONSTANT(DEVICE_TYPE_INTEGRATED_GPU);
	BIND_ENUM_CONSTANT(DEVICE_TYPE_DISCRETE_GPU);
	BIND_ENUM_CONSTANT(DEVICE_TYPE_VIRTUAL_GPU);
	BIND_ENUM_CONSTANT(DEVICE_TYPE_CPU);
	BIND_ENUM_CONSTANT(DEVICE_TYPE_MAX);

	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_LOGICAL_DEVICE);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_PHYSICAL_DEVICE);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_TOPMOST_OBJECT);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_COMMAND_QUEUE);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_QUEUE_FAMILY);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_TEXTURE);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_TEXTURE_VIEW);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_TEXTURE_DATA_FORMAT);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_SAMPLER);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_UNIFORM_SET);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_BUFFER);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_COMPUTE_PIPELINE);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_RENDER_PIPELINE);
#ifndef DISABLE_DEPRECATED
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_VULKAN_DEVICE);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_VULKAN_PHYSICAL_DEVICE);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_VULKAN_INSTANCE);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_VULKAN_QUEUE);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_VULKAN_QUEUE_FAMILY_INDEX);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_VULKAN_IMAGE);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_VULKAN_IMAGE_VIEW);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_VULKAN_IMAGE_NATIVE_TEXTURE_FORMAT);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_VULKAN_SAMPLER);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_VULKAN_DESCRIPTOR_SET);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_VULKAN_BUFFER);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_VULKAN_COMPUTE_PIPELINE);
	BIND_ENUM_CONSTANT(DRIVER_RESOURCE_VULKAN_RENDER_PIPELINE);
#endif

	BIND_ENUM_CONSTANT(DATA_FORMAT_R4G4_UNORM_PACK8);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R4G4B4A4_UNORM_PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B4G4R4A4_UNORM_PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R5G6B5_UNORM_PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B5G6R5_UNORM_PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R5G5B5A1_UNORM_PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B5G5R5A1_UNORM_PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A1R5G5B5_UNORM_PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8_SNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8_USCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8_SSCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8_SRGB);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8_SNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8_USCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8_SSCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8_SRGB);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8B8_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8B8_SNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8B8_USCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8B8_SSCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8B8_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8B8_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8B8_SRGB);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B8G8R8_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B8G8R8_SNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B8G8R8_USCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B8G8R8_SSCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B8G8R8_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B8G8R8_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B8G8R8_SRGB);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8B8A8_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8B8A8_SNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8B8A8_USCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8B8A8_SSCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8B8A8_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8B8A8_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R8G8B8A8_SRGB);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B8G8R8A8_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B8G8R8A8_SNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B8G8R8A8_USCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B8G8R8A8_SSCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B8G8R8A8_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B8G8R8A8_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B8G8R8A8_SRGB);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A8B8G8R8_UNORM_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A8B8G8R8_SNORM_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A8B8G8R8_USCALED_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A8B8G8R8_SSCALED_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A8B8G8R8_UINT_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A8B8G8R8_SINT_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A8B8G8R8_SRGB_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A2R10G10B10_UNORM_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A2R10G10B10_SNORM_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A2R10G10B10_USCALED_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A2R10G10B10_SSCALED_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A2R10G10B10_UINT_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A2R10G10B10_SINT_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A2B10G10R10_UNORM_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A2B10G10R10_SNORM_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A2B10G10R10_USCALED_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A2B10G10R10_SSCALED_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A2B10G10R10_UINT_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_A2B10G10R10_SINT_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16_SNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16_USCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16_SSCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16_SFLOAT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16_SNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16_USCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16_SSCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16_SFLOAT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16B16_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16B16_SNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16B16_USCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16B16_SSCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16B16_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16B16_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16B16_SFLOAT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16B16A16_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16B16A16_SNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16B16A16_USCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16B16A16_SSCALED);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16B16A16_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16B16A16_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R16G16B16A16_SFLOAT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R32_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R32_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R32_SFLOAT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R32G32_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R32G32_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R32G32_SFLOAT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R32G32B32_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R32G32B32_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R32G32B32_SFLOAT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R32G32B32A32_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R32G32B32A32_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R32G32B32A32_SFLOAT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R64_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R64_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R64_SFLOAT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R64G64_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R64G64_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R64G64_SFLOAT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R64G64B64_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R64G64B64_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R64G64B64_SFLOAT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R64G64B64A64_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R64G64B64A64_SINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R64G64B64A64_SFLOAT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B10G11R11_UFLOAT_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_E5B9G9R9_UFLOAT_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_D16_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_X8_D24_UNORM_PACK32);
	BIND_ENUM_CONSTANT(DATA_FORMAT_D32_SFLOAT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_S8_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_D16_UNORM_S8_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_D24_UNORM_S8_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_D32_SFLOAT_S8_UINT);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC1_RGB_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC1_RGB_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC1_RGBA_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC1_RGBA_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC2_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC2_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC3_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC3_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC4_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC4_SNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC5_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC5_SNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC6H_UFLOAT_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC6H_SFLOAT_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC7_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_BC7_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ETC2_R8G8B8_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ETC2_R8G8B8_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_EAC_R11_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_EAC_R11_SNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_EAC_R11G11_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_EAC_R11G11_SNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_4x4_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_4x4_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_5x4_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_5x4_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_5x5_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_5x5_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_6x5_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_6x5_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_6x6_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_6x6_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_8x5_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_8x5_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_8x6_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_8x6_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_8x8_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_8x8_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_10x5_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_10x5_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_10x6_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_10x6_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_10x8_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_10x8_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_10x10_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_10x10_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_12x10_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_12x10_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_12x12_UNORM_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_ASTC_12x12_SRGB_BLOCK);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G8B8G8R8_422_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B8G8R8G8_422_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G8_B8R8_2PLANE_420_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G8_B8_R8_3PLANE_422_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G8_B8R8_2PLANE_422_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G8_B8_R8_3PLANE_444_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R10X6_UNORM_PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R10X6G10X6_UNORM_2PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R12X4_UNORM_PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R12X4G12X4_UNORM_2PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G16B16G16R16_422_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_B16G16R16G16_422_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G16_B16_R16_3PLANE_420_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G16_B16R16_2PLANE_420_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G16_B16_R16_3PLANE_422_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G16_B16R16_2PLANE_422_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_G16_B16_R16_3PLANE_444_UNORM);
	BIND_ENUM_CONSTANT(DATA_FORMAT_MAX);

#ifndef DISABLE_DEPRECATED
	BIND_BITFIELD_FLAG(BARRIER_MASK_VERTEX);
	BIND_BITFIELD_FLAG(BARRIER_MASK_FRAGMENT);
	BIND_BITFIELD_FLAG(BARRIER_MASK_COMPUTE);
	BIND_BITFIELD_FLAG(BARRIER_MASK_TRANSFER);
	BIND_BITFIELD_FLAG(BARRIER_MASK_RASTER);
	BIND_BITFIELD_FLAG(BARRIER_MASK_ALL_BARRIERS);
	BIND_BITFIELD_FLAG(BARRIER_MASK_NO_BARRIER);
#endif

	BIND_ENUM_CONSTANT(TEXTURE_TYPE_1D);
	BIND_ENUM_CONSTANT(TEXTURE_TYPE_2D);
	BIND_ENUM_CONSTANT(TEXTURE_TYPE_3D);
	BIND_ENUM_CONSTANT(TEXTURE_TYPE_CUBE);
	BIND_ENUM_CONSTANT(TEXTURE_TYPE_1D_ARRAY);
	BIND_ENUM_CONSTANT(TEXTURE_TYPE_2D_ARRAY);
	BIND_ENUM_CONSTANT(TEXTURE_TYPE_CUBE_ARRAY);
	BIND_ENUM_CONSTANT(TEXTURE_TYPE_MAX);

	BIND_ENUM_CONSTANT(TEXTURE_SAMPLES_1);
	BIND_ENUM_CONSTANT(TEXTURE_SAMPLES_2);
	BIND_ENUM_CONSTANT(TEXTURE_SAMPLES_4);
	BIND_ENUM_CONSTANT(TEXTURE_SAMPLES_8);
	BIND_ENUM_CONSTANT(TEXTURE_SAMPLES_16);
	BIND_ENUM_CONSTANT(TEXTURE_SAMPLES_32);
	BIND_ENUM_CONSTANT(TEXTURE_SAMPLES_64);
	BIND_ENUM_CONSTANT(TEXTURE_SAMPLES_MAX);

	BIND_BITFIELD_FLAG(TEXTURE_USAGE_SAMPLING_BIT);
	BIND_BITFIELD_FLAG(TEXTURE_USAGE_COLOR_ATTACHMENT_BIT);
	BIND_BITFIELD_FLAG(TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	BIND_BITFIELD_FLAG(TEXTURE_USAGE_STORAGE_BIT);
	BIND_BITFIELD_FLAG(TEXTURE_USAGE_STORAGE_ATOMIC_BIT);
	BIND_BITFIELD_FLAG(TEXTURE_USAGE_CPU_READ_BIT);
	BIND_BITFIELD_FLAG(TEXTURE_USAGE_CAN_UPDATE_BIT);
	BIND_BITFIELD_FLAG(TEXTURE_USAGE_CAN_COPY_FROM_BIT);
	BIND_BITFIELD_FLAG(TEXTURE_USAGE_CAN_COPY_TO_BIT);
	BIND_BITFIELD_FLAG(TEXTURE_USAGE_INPUT_ATTACHMENT_BIT);

	BIND_ENUM_CONSTANT(TEXTURE_SWIZZLE_IDENTITY);
	BIND_ENUM_CONSTANT(TEXTURE_SWIZZLE_ZERO);
	BIND_ENUM_CONSTANT(TEXTURE_SWIZZLE_ONE);
	BIND_ENUM_CONSTANT(TEXTURE_SWIZZLE_R);
	BIND_ENUM_CONSTANT(TEXTURE_SWIZZLE_G);
	BIND_ENUM_CONSTANT(TEXTURE_SWIZZLE_B);
	BIND_ENUM_CONSTANT(TEXTURE_SWIZZLE_A);
	BIND_ENUM_CONSTANT(TEXTURE_SWIZZLE_MAX);

	BIND_ENUM_CONSTANT(TEXTURE_SLICE_2D);
	BIND_ENUM_CONSTANT(TEXTURE_SLICE_CUBEMAP);
	BIND_ENUM_CONSTANT(TEXTURE_SLICE_3D);

	BIND_ENUM_CONSTANT(SAMPLER_FILTER_NEAREST);
	BIND_ENUM_CONSTANT(SAMPLER_FILTER_LINEAR);
	BIND_ENUM_CONSTANT(SAMPLER_REPEAT_MODE_REPEAT);
	BIND_ENUM_CONSTANT(SAMPLER_REPEAT_MODE_MIRRORED_REPEAT);
	BIND_ENUM_CONSTANT(SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
	BIND_ENUM_CONSTANT(SAMPLER_REPEAT_MODE_CLAMP_TO_BORDER);
	BIND_ENUM_CONSTANT(SAMPLER_REPEAT_MODE_MIRROR_CLAMP_TO_EDGE);
	BIND_ENUM_CONSTANT(SAMPLER_REPEAT_MODE_MAX);

	BIND_ENUM_CONSTANT(SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK);
	BIND_ENUM_CONSTANT(SAMPLER_BORDER_COLOR_INT_TRANSPARENT_BLACK);
	BIND_ENUM_CONSTANT(SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_BLACK);
	BIND_ENUM_CONSTANT(SAMPLER_BORDER_COLOR_INT_OPAQUE_BLACK);
	BIND_ENUM_CONSTANT(SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_WHITE);
	BIND_ENUM_CONSTANT(SAMPLER_BORDER_COLOR_INT_OPAQUE_WHITE);
	BIND_ENUM_CONSTANT(SAMPLER_BORDER_COLOR_MAX);

	BIND_ENUM_CONSTANT(VERTEX_FREQUENCY_VERTEX);
	BIND_ENUM_CONSTANT(VERTEX_FREQUENCY_INSTANCE);

	BIND_ENUM_CONSTANT(INDEX_BUFFER_FORMAT_UINT16);
	BIND_ENUM_CONSTANT(INDEX_BUFFER_FORMAT_UINT32);

	BIND_BITFIELD_FLAG(STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);

	BIND_ENUM_CONSTANT(UNIFORM_TYPE_SAMPLER); //for sampling only (sampler GLSL type)
	BIND_ENUM_CONSTANT(UNIFORM_TYPE_SAMPLER_WITH_TEXTURE); // for sampling only); but includes a texture); (samplerXX GLSL type)); first a sampler then a texture
	BIND_ENUM_CONSTANT(UNIFORM_TYPE_TEXTURE); //only texture); (textureXX GLSL type)
	BIND_ENUM_CONSTANT(UNIFORM_TYPE_IMAGE); // storage image (imageXX GLSL type)); for compute mostly
	BIND_ENUM_CONSTANT(UNIFORM_TYPE_TEXTURE_BUFFER); // buffer texture (or TBO); textureBuffer type)
	BIND_ENUM_CONSTANT(UNIFORM_TYPE_SAMPLER_WITH_TEXTURE_BUFFER); // buffer texture with a sampler(or TBO); samplerBuffer type)
	BIND_ENUM_CONSTANT(UNIFORM_TYPE_IMAGE_BUFFER); //texel buffer); (imageBuffer type)); for compute mostly
	BIND_ENUM_CONSTANT(UNIFORM_TYPE_UNIFORM_BUFFER); //regular uniform buffer (or UBO).
	BIND_ENUM_CONSTANT(UNIFORM_TYPE_STORAGE_BUFFER); //storage buffer ("buffer" qualifier) like UBO); but supports storage); for compute mostly
	BIND_ENUM_CONSTANT(UNIFORM_TYPE_INPUT_ATTACHMENT); //used for sub-pass read/write); for mobile mostly
	BIND_ENUM_CONSTANT(UNIFORM_TYPE_MAX);

	BIND_ENUM_CONSTANT(RENDER_PRIMITIVE_POINTS);
	BIND_ENUM_CONSTANT(RENDER_PRIMITIVE_LINES);
	BIND_ENUM_CONSTANT(RENDER_PRIMITIVE_LINES_WITH_ADJACENCY);
	BIND_ENUM_CONSTANT(RENDER_PRIMITIVE_LINESTRIPS);
	BIND_ENUM_CONSTANT(RENDER_PRIMITIVE_LINESTRIPS_WITH_ADJACENCY);
	BIND_ENUM_CONSTANT(RENDER_PRIMITIVE_TRIANGLES);
	BIND_ENUM_CONSTANT(RENDER_PRIMITIVE_TRIANGLES_WITH_ADJACENCY);
	BIND_ENUM_CONSTANT(RENDER_PRIMITIVE_TRIANGLE_STRIPS);
	BIND_ENUM_CONSTANT(RENDER_PRIMITIVE_TRIANGLE_STRIPS_WITH_AJACENCY);
	BIND_ENUM_CONSTANT(RENDER_PRIMITIVE_TRIANGLE_STRIPS_WITH_RESTART_INDEX);
	BIND_ENUM_CONSTANT(RENDER_PRIMITIVE_TESSELATION_PATCH);
	BIND_ENUM_CONSTANT(RENDER_PRIMITIVE_MAX);

	BIND_ENUM_CONSTANT(POLYGON_CULL_DISABLED);
	BIND_ENUM_CONSTANT(POLYGON_CULL_FRONT);
	BIND_ENUM_CONSTANT(POLYGON_CULL_BACK);

	BIND_ENUM_CONSTANT(POLYGON_FRONT_FACE_CLOCKWISE);
	BIND_ENUM_CONSTANT(POLYGON_FRONT_FACE_COUNTER_CLOCKWISE);

	BIND_ENUM_CONSTANT(STENCIL_OP_KEEP);
	BIND_ENUM_CONSTANT(STENCIL_OP_ZERO);
	BIND_ENUM_CONSTANT(STENCIL_OP_REPLACE);
	BIND_ENUM_CONSTANT(STENCIL_OP_INCREMENT_AND_CLAMP);
	BIND_ENUM_CONSTANT(STENCIL_OP_DECREMENT_AND_CLAMP);
	BIND_ENUM_CONSTANT(STENCIL_OP_INVERT);
	BIND_ENUM_CONSTANT(STENCIL_OP_INCREMENT_AND_WRAP);
	BIND_ENUM_CONSTANT(STENCIL_OP_DECREMENT_AND_WRAP);
	BIND_ENUM_CONSTANT(STENCIL_OP_MAX); //not an actual operator); just the amount of operators :D

	BIND_ENUM_CONSTANT(COMPARE_OP_NEVER);
	BIND_ENUM_CONSTANT(COMPARE_OP_LESS);
	BIND_ENUM_CONSTANT(COMPARE_OP_EQUAL);
	BIND_ENUM_CONSTANT(COMPARE_OP_LESS_OR_EQUAL);
	BIND_ENUM_CONSTANT(COMPARE_OP_GREATER);
	BIND_ENUM_CONSTANT(COMPARE_OP_NOT_EQUAL);
	BIND_ENUM_CONSTANT(COMPARE_OP_GREATER_OR_EQUAL);
	BIND_ENUM_CONSTANT(COMPARE_OP_ALWAYS);
	BIND_ENUM_CONSTANT(COMPARE_OP_MAX);

	BIND_ENUM_CONSTANT(LOGIC_OP_CLEAR);
	BIND_ENUM_CONSTANT(LOGIC_OP_AND);
	BIND_ENUM_CONSTANT(LOGIC_OP_AND_REVERSE);
	BIND_ENUM_CONSTANT(LOGIC_OP_COPY);
	BIND_ENUM_CONSTANT(LOGIC_OP_AND_INVERTED);
	BIND_ENUM_CONSTANT(LOGIC_OP_NO_OP);
	BIND_ENUM_CONSTANT(LOGIC_OP_XOR);
	BIND_ENUM_CONSTANT(LOGIC_OP_OR);
	BIND_ENUM_CONSTANT(LOGIC_OP_NOR);
	BIND_ENUM_CONSTANT(LOGIC_OP_EQUIVALENT);
	BIND_ENUM_CONSTANT(LOGIC_OP_INVERT);
	BIND_ENUM_CONSTANT(LOGIC_OP_OR_REVERSE);
	BIND_ENUM_CONSTANT(LOGIC_OP_COPY_INVERTED);
	BIND_ENUM_CONSTANT(LOGIC_OP_OR_INVERTED);
	BIND_ENUM_CONSTANT(LOGIC_OP_NAND);
	BIND_ENUM_CONSTANT(LOGIC_OP_SET);
	BIND_ENUM_CONSTANT(LOGIC_OP_MAX); //not an actual operator); just the amount of operators :D

	BIND_ENUM_CONSTANT(BLEND_FACTOR_ZERO);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_ONE);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_SRC_COLOR);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_ONE_MINUS_SRC_COLOR);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_DST_COLOR);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_ONE_MINUS_DST_COLOR);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_SRC_ALPHA);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_DST_ALPHA);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_ONE_MINUS_DST_ALPHA);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_CONSTANT_COLOR);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_CONSTANT_ALPHA);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_SRC_ALPHA_SATURATE);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_SRC1_COLOR);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_ONE_MINUS_SRC1_COLOR);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_SRC1_ALPHA);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_MAX);

	BIND_ENUM_CONSTANT(BLEND_OP_ADD);
	BIND_ENUM_CONSTANT(BLEND_OP_SUBTRACT);
	BIND_ENUM_CONSTANT(BLEND_OP_REVERSE_SUBTRACT);
	BIND_ENUM_CONSTANT(BLEND_OP_MINIMUM);
	BIND_ENUM_CONSTANT(BLEND_OP_MAXIMUM);
	BIND_ENUM_CONSTANT(BLEND_OP_MAX);

	BIND_BITFIELD_FLAG(DYNAMIC_STATE_LINE_WIDTH);
	BIND_BITFIELD_FLAG(DYNAMIC_STATE_DEPTH_BIAS);
	BIND_BITFIELD_FLAG(DYNAMIC_STATE_BLEND_CONSTANTS);
	BIND_BITFIELD_FLAG(DYNAMIC_STATE_DEPTH_BOUNDS);
	BIND_BITFIELD_FLAG(DYNAMIC_STATE_STENCIL_COMPARE_MASK);
	BIND_BITFIELD_FLAG(DYNAMIC_STATE_STENCIL_WRITE_MASK);
	BIND_BITFIELD_FLAG(DYNAMIC_STATE_STENCIL_REFERENCE);

#ifndef DISABLE_DEPRECATED
	BIND_ENUM_CONSTANT(INITIAL_ACTION_LOAD);
	BIND_ENUM_CONSTANT(INITIAL_ACTION_CLEAR);
	BIND_ENUM_CONSTANT(INITIAL_ACTION_DISCARD);
	BIND_ENUM_CONSTANT(INITIAL_ACTION_MAX);
	BIND_ENUM_CONSTANT(INITIAL_ACTION_CLEAR_REGION);
	BIND_ENUM_CONSTANT(INITIAL_ACTION_CLEAR_REGION_CONTINUE);
	BIND_ENUM_CONSTANT(INITIAL_ACTION_KEEP);
	BIND_ENUM_CONSTANT(INITIAL_ACTION_DROP);
	BIND_ENUM_CONSTANT(INITIAL_ACTION_CONTINUE);

	BIND_ENUM_CONSTANT(FINAL_ACTION_STORE);
	BIND_ENUM_CONSTANT(FINAL_ACTION_DISCARD);
	BIND_ENUM_CONSTANT(FINAL_ACTION_MAX);
	BIND_ENUM_CONSTANT(FINAL_ACTION_READ);
	BIND_ENUM_CONSTANT(FINAL_ACTION_CONTINUE);
#endif

	BIND_ENUM_CONSTANT(SHADER_STAGE_VERTEX);
	BIND_ENUM_CONSTANT(SHADER_STAGE_FRAGMENT);
	BIND_ENUM_CONSTANT(SHADER_STAGE_TESSELATION_CONTROL);
	BIND_ENUM_CONSTANT(SHADER_STAGE_TESSELATION_EVALUATION);
	BIND_ENUM_CONSTANT(SHADER_STAGE_COMPUTE);
	BIND_ENUM_CONSTANT(SHADER_STAGE_MAX);
	BIND_ENUM_CONSTANT(SHADER_STAGE_VERTEX_BIT);
	BIND_ENUM_CONSTANT(SHADER_STAGE_FRAGMENT_BIT);
	BIND_ENUM_CONSTANT(SHADER_STAGE_TESSELATION_CONTROL_BIT);
	BIND_ENUM_CONSTANT(SHADER_STAGE_TESSELATION_EVALUATION_BIT);
	BIND_ENUM_CONSTANT(SHADER_STAGE_COMPUTE_BIT);

	BIND_ENUM_CONSTANT(SHADER_LANGUAGE_GLSL);
	BIND_ENUM_CONSTANT(SHADER_LANGUAGE_HLSL);

	BIND_ENUM_CONSTANT(PIPELINE_SPECIALIZATION_CONSTANT_TYPE_BOOL);
	BIND_ENUM_CONSTANT(PIPELINE_SPECIALIZATION_CONSTANT_TYPE_INT);
	BIND_ENUM_CONSTANT(PIPELINE_SPECIALIZATION_CONSTANT_TYPE_FLOAT);

	BIND_ENUM_CONSTANT(LIMIT_MAX_BOUND_UNIFORM_SETS);
	BIND_ENUM_CONSTANT(LIMIT_MAX_FRAMEBUFFER_COLOR_ATTACHMENTS);
	BIND_ENUM_CONSTANT(LIMIT_MAX_TEXTURES_PER_UNIFORM_SET);
	BIND_ENUM_CONSTANT(LIMIT_MAX_SAMPLERS_PER_UNIFORM_SET);
	BIND_ENUM_CONSTANT(LIMIT_MAX_STORAGE_BUFFERS_PER_UNIFORM_SET);
	BIND_ENUM_CONSTANT(LIMIT_MAX_STORAGE_IMAGES_PER_UNIFORM_SET);
	BIND_ENUM_CONSTANT(LIMIT_MAX_UNIFORM_BUFFERS_PER_UNIFORM_SET);
	BIND_ENUM_CONSTANT(LIMIT_MAX_DRAW_INDEXED_INDEX);
	BIND_ENUM_CONSTANT(LIMIT_MAX_FRAMEBUFFER_HEIGHT);
	BIND_ENUM_CONSTANT(LIMIT_MAX_FRAMEBUFFER_WIDTH);
	BIND_ENUM_CONSTANT(LIMIT_MAX_TEXTURE_ARRAY_LAYERS);
	BIND_ENUM_CONSTANT(LIMIT_MAX_TEXTURE_SIZE_1D);
	BIND_ENUM_CONSTANT(LIMIT_MAX_TEXTURE_SIZE_2D);
	BIND_ENUM_CONSTANT(LIMIT_MAX_TEXTURE_SIZE_3D);
	BIND_ENUM_CONSTANT(LIMIT_MAX_TEXTURE_SIZE_CUBE);
	BIND_ENUM_CONSTANT(LIMIT_MAX_TEXTURES_PER_SHADER_STAGE);
	BIND_ENUM_CONSTANT(LIMIT_MAX_SAMPLERS_PER_SHADER_STAGE);
	BIND_ENUM_CONSTANT(LIMIT_MAX_STORAGE_BUFFERS_PER_SHADER_STAGE);
	BIND_ENUM_CONSTANT(LIMIT_MAX_STORAGE_IMAGES_PER_SHADER_STAGE);
	BIND_ENUM_CONSTANT(LIMIT_MAX_UNIFORM_BUFFERS_PER_SHADER_STAGE);
	BIND_ENUM_CONSTANT(LIMIT_MAX_PUSH_CONSTANT_SIZE);
	BIND_ENUM_CONSTANT(LIMIT_MAX_UNIFORM_BUFFER_SIZE);
	BIND_ENUM_CONSTANT(LIMIT_MAX_VERTEX_INPUT_ATTRIBUTE_OFFSET);
	BIND_ENUM_CONSTANT(LIMIT_MAX_VERTEX_INPUT_ATTRIBUTES);
	BIND_ENUM_CONSTANT(LIMIT_MAX_VERTEX_INPUT_BINDINGS);
	BIND_ENUM_CONSTANT(LIMIT_MAX_VERTEX_INPUT_BINDING_STRIDE);
	BIND_ENUM_CONSTANT(LIMIT_MIN_UNIFORM_BUFFER_OFFSET_ALIGNMENT);
	BIND_ENUM_CONSTANT(LIMIT_MAX_COMPUTE_SHARED_MEMORY_SIZE);
	BIND_ENUM_CONSTANT(LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X);
	BIND_ENUM_CONSTANT(LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_Y);
	BIND_ENUM_CONSTANT(LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_Z);
	BIND_ENUM_CONSTANT(LIMIT_MAX_COMPUTE_WORKGROUP_INVOCATIONS);
	BIND_ENUM_CONSTANT(LIMIT_MAX_COMPUTE_WORKGROUP_SIZE_X);
	BIND_ENUM_CONSTANT(LIMIT_MAX_COMPUTE_WORKGROUP_SIZE_Y);
	BIND_ENUM_CONSTANT(LIMIT_MAX_COMPUTE_WORKGROUP_SIZE_Z);
	BIND_ENUM_CONSTANT(LIMIT_MAX_VIEWPORT_DIMENSIONS_X);
	BIND_ENUM_CONSTANT(LIMIT_MAX_VIEWPORT_DIMENSIONS_Y);
	BIND_ENUM_CONSTANT(LIMIT_METALFX_TEMPORAL_SCALER_MIN_SCALE);
	BIND_ENUM_CONSTANT(LIMIT_METALFX_TEMPORAL_SCALER_MAX_SCALE);

	BIND_ENUM_CONSTANT(MEMORY_TEXTURES);
	BIND_ENUM_CONSTANT(MEMORY_BUFFERS);
	BIND_ENUM_CONSTANT(MEMORY_TOTAL);

	BIND_CONSTANT(INVALID_ID);
	BIND_CONSTANT(INVALID_FORMAT_ID);

	BIND_ENUM_CONSTANT(NONE);
	BIND_ENUM_CONSTANT(REFLECTION_PROBES);
	BIND_ENUM_CONSTANT(SKY_PASS);
	BIND_ENUM_CONSTANT(LIGHTMAPPER_PASS);
	BIND_ENUM_CONSTANT(SHADOW_PASS_DIRECTIONAL);
	BIND_ENUM_CONSTANT(SHADOW_PASS_CUBE);
	BIND_ENUM_CONSTANT(OPAQUE_PASS);
	BIND_ENUM_CONSTANT(ALPHA_PASS);
	BIND_ENUM_CONSTANT(TRANSPARENT_PASS);
	BIND_ENUM_CONSTANT(POST_PROCESSING_PASS);
	BIND_ENUM_CONSTANT(BLIT_PASS);
	BIND_ENUM_CONSTANT(UI_PASS);
	BIND_ENUM_CONSTANT(DEBUG_PASS);

	BIND_BITFIELD_FLAG(DRAW_DEFAULT_ALL);
	BIND_BITFIELD_FLAG(DRAW_CLEAR_COLOR_0);
	BIND_BITFIELD_FLAG(DRAW_CLEAR_COLOR_1);
	BIND_BITFIELD_FLAG(DRAW_CLEAR_COLOR_2);
	BIND_BITFIELD_FLAG(DRAW_CLEAR_COLOR_3);
	BIND_BITFIELD_FLAG(DRAW_CLEAR_COLOR_4);
	BIND_BITFIELD_FLAG(DRAW_CLEAR_COLOR_5);
	BIND_BITFIELD_FLAG(DRAW_CLEAR_COLOR_6);
	BIND_BITFIELD_FLAG(DRAW_CLEAR_COLOR_7);
	BIND_BITFIELD_FLAG(DRAW_CLEAR_COLOR_MASK);
	BIND_BITFIELD_FLAG(DRAW_CLEAR_COLOR_ALL);
	BIND_BITFIELD_FLAG(DRAW_IGNORE_COLOR_0);
	BIND_BITFIELD_FLAG(DRAW_IGNORE_COLOR_1);
	BIND_BITFIELD_FLAG(DRAW_IGNORE_COLOR_2);
	BIND_BITFIELD_FLAG(DRAW_IGNORE_COLOR_3);
	BIND_BITFIELD_FLAG(DRAW_IGNORE_COLOR_4);
	BIND_BITFIELD_FLAG(DRAW_IGNORE_COLOR_5);
	BIND_BITFIELD_FLAG(DRAW_IGNORE_COLOR_6);
	BIND_BITFIELD_FLAG(DRAW_IGNORE_COLOR_7);
	BIND_BITFIELD_FLAG(DRAW_IGNORE_COLOR_MASK);
	BIND_BITFIELD_FLAG(DRAW_IGNORE_COLOR_ALL);
	BIND_BITFIELD_FLAG(DRAW_CLEAR_DEPTH);
	BIND_BITFIELD_FLAG(DRAW_IGNORE_DEPTH);
	BIND_BITFIELD_FLAG(DRAW_CLEAR_STENCIL);
	BIND_BITFIELD_FLAG(DRAW_IGNORE_STENCIL);
	BIND_BITFIELD_FLAG(DRAW_CLEAR_ALL);
	BIND_BITFIELD_FLAG(DRAW_IGNORE_ALL);
}

void RenderingDevice::make_current() {
	render_thread_id = Thread::get_caller_id();
}

RenderingDevice::~RenderingDevice() {
	finalize();

	if (singleton == this) {
		singleton = nullptr;
	}
}

RenderingDevice::RenderingDevice() {
	if (singleton == nullptr) {
		singleton = this;
	}

	render_thread_id = Thread::get_caller_id();
}

/*****************/
/**** BINDERS ****/
/*****************/

RID RenderingDevice::_texture_create(const Ref<RDTextureFormat> &p_format, const Ref<RDTextureView> &p_view, const TypedArray<PackedByteArray> &p_data) {
	ERR_FAIL_COND_V(p_format.is_null(), RID());
	ERR_FAIL_COND_V(p_view.is_null(), RID());
	Vector<Vector<uint8_t>> data;
	for (int i = 0; i < p_data.size(); i++) {
		Vector<uint8_t> byte_slice = p_data[i];
		ERR_FAIL_COND_V(byte_slice.is_empty(), RID());
		data.push_back(byte_slice);
	}
	return texture_create(p_format->base, p_view->base, data);
}

RID RenderingDevice::_texture_create_shared(const Ref<RDTextureView> &p_view, RID p_with_texture) {
	ERR_FAIL_COND_V(p_view.is_null(), RID());

	return texture_create_shared(p_view->base, p_with_texture);
}

RID RenderingDevice::_texture_create_shared_from_slice(const Ref<RDTextureView> &p_view, RID p_with_texture, uint32_t p_layer, uint32_t p_mipmap, uint32_t p_mipmaps, TextureSliceType p_slice_type) {
	ERR_FAIL_COND_V(p_view.is_null(), RID());

	return texture_create_shared_from_slice(p_view->base, p_with_texture, p_layer, p_mipmap, p_mipmaps, p_slice_type);
}

Ref<RDTextureFormat> RenderingDevice::_texture_get_format(RID p_rd_texture) {
	Ref<RDTextureFormat> rtf;
	rtf.instantiate();
	rtf->base = texture_get_format(p_rd_texture);

	return rtf;
}

RenderingDevice::FramebufferFormatID RenderingDevice::_framebuffer_format_create(const TypedArray<RDAttachmentFormat> &p_attachments, uint32_t p_view_count) {
	Vector<AttachmentFormat> attachments;
	attachments.resize(p_attachments.size());

	for (int i = 0; i < p_attachments.size(); i++) {
		Ref<RDAttachmentFormat> af = p_attachments[i];
		ERR_FAIL_COND_V(af.is_null(), INVALID_FORMAT_ID);
		attachments.write[i] = af->base;
	}
	return framebuffer_format_create(attachments, p_view_count);
}

RenderingDevice::FramebufferFormatID RenderingDevice::_framebuffer_format_create_multipass(const TypedArray<RDAttachmentFormat> &p_attachments, const TypedArray<RDFramebufferPass> &p_passes, uint32_t p_view_count) {
	Vector<AttachmentFormat> attachments;
	attachments.resize(p_attachments.size());

	for (int i = 0; i < p_attachments.size(); i++) {
		Ref<RDAttachmentFormat> af = p_attachments[i];
		ERR_FAIL_COND_V(af.is_null(), INVALID_FORMAT_ID);
		attachments.write[i] = af->base;
	}

	Vector<FramebufferPass> passes;
	for (int i = 0; i < p_passes.size(); i++) {
		Ref<RDFramebufferPass> pass = p_passes[i];
		ERR_CONTINUE(pass.is_null());
		passes.push_back(pass->base);
	}

	return framebuffer_format_create_multipass(attachments, passes, p_view_count);
}

RID RenderingDevice::_framebuffer_create(const TypedArray<RID> &p_textures, FramebufferFormatID p_format_check, uint32_t p_view_count) {
	Vector<RID> textures = Variant(p_textures);
	return framebuffer_create(textures, p_format_check, p_view_count);
}

RID RenderingDevice::_framebuffer_create_multipass(const TypedArray<RID> &p_textures, const TypedArray<RDFramebufferPass> &p_passes, FramebufferFormatID p_format_check, uint32_t p_view_count) {
	Vector<RID> textures = Variant(p_textures);
	Vector<FramebufferPass> passes;
	for (int i = 0; i < p_passes.size(); i++) {
		Ref<RDFramebufferPass> pass = p_passes[i];
		ERR_CONTINUE(pass.is_null());
		passes.push_back(pass->base);
	}
	return framebuffer_create_multipass(textures, passes, p_format_check, p_view_count);
}

RID RenderingDevice::_sampler_create(const Ref<RDSamplerState> &p_state) {
	ERR_FAIL_COND_V(p_state.is_null(), RID());

	return sampler_create(p_state->base);
}

RenderingDevice::VertexFormatID RenderingDevice::_vertex_format_create(const TypedArray<RDVertexAttribute> &p_vertex_formats) {
	Vector<VertexAttribute> descriptions;
	descriptions.resize(p_vertex_formats.size());

	for (int i = 0; i < p_vertex_formats.size(); i++) {
		Ref<RDVertexAttribute> af = p_vertex_formats[i];
		ERR_FAIL_COND_V(af.is_null(), INVALID_FORMAT_ID);
		descriptions.write[i] = af->base;
	}
	return vertex_format_create(descriptions);
}

RID RenderingDevice::_vertex_array_create(uint32_t p_vertex_count, VertexFormatID p_vertex_format, const TypedArray<RID> &p_src_buffers, const Vector<int64_t> &p_offsets) {
	Vector<RID> buffers = Variant(p_src_buffers);

	Vector<uint64_t> offsets;
	offsets.resize(p_offsets.size());
	for (int i = 0; i < p_offsets.size(); i++) {
		offsets.write[i] = p_offsets[i];
	}

	return vertex_array_create(p_vertex_count, p_vertex_format, buffers, offsets);
}

Ref<RDShaderSPIRV> RenderingDevice::_shader_compile_spirv_from_source(const Ref<RDShaderSource> &p_source, bool p_allow_cache) {
	ERR_FAIL_COND_V(p_source.is_null(), Ref<RDShaderSPIRV>());

	Ref<RDShaderSPIRV> bytecode;
	bytecode.instantiate();
	for (int i = 0; i < RD::SHADER_STAGE_MAX; i++) {
		String error;

		ShaderStage stage = ShaderStage(i);
		String source = p_source->get_stage_source(stage);

		if (!source.is_empty()) {
			Vector<uint8_t> spirv = shader_compile_spirv_from_source(stage, source, p_source->get_language(), &error, p_allow_cache);
			bytecode->set_stage_bytecode(stage, spirv);
			bytecode->set_stage_compile_error(stage, error);
		}
	}
	return bytecode;
}

Vector<uint8_t> RenderingDevice::_shader_compile_binary_from_spirv(const Ref<RDShaderSPIRV> &p_spirv, const String &p_shader_name) {
	ERR_FAIL_COND_V(p_spirv.is_null(), Vector<uint8_t>());

	Vector<ShaderStageSPIRVData> stage_data;
	for (int i = 0; i < RD::SHADER_STAGE_MAX; i++) {
		ShaderStage stage = ShaderStage(i);
		ShaderStageSPIRVData sd;
		sd.shader_stage = stage;
		String error = p_spirv->get_stage_compile_error(stage);
		ERR_FAIL_COND_V_MSG(!error.is_empty(), Vector<uint8_t>(), "Can't create a shader from an errored bytecode. Check errors in source bytecode.");
		sd.spirv = p_spirv->get_stage_bytecode(stage);
		if (sd.spirv.is_empty()) {
			continue;
		}
		stage_data.push_back(sd);
	}

	return shader_compile_binary_from_spirv(stage_data, p_shader_name);
}

RID RenderingDevice::_shader_create_from_spirv(const Ref<RDShaderSPIRV> &p_spirv, const String &p_shader_name) {
	ERR_FAIL_COND_V(p_spirv.is_null(), RID());

	Vector<ShaderStageSPIRVData> stage_data;
	for (int i = 0; i < RD::SHADER_STAGE_MAX; i++) {
		ShaderStage stage = ShaderStage(i);
		ShaderStageSPIRVData sd;
		sd.shader_stage = stage;
		String error = p_spirv->get_stage_compile_error(stage);
		ERR_FAIL_COND_V_MSG(!error.is_empty(), RID(), "Can't create a shader from an errored bytecode. Check errors in source bytecode.");
		sd.spirv = p_spirv->get_stage_bytecode(stage);
		if (sd.spirv.is_empty()) {
			continue;
		}
		stage_data.push_back(sd);
	}
	return shader_create_from_spirv(stage_data);
}

RID RenderingDevice::_uniform_set_create(const TypedArray<RDUniform> &p_uniforms, RID p_shader, uint32_t p_shader_set) {
	Vector<Uniform> uniforms;
	uniforms.resize(p_uniforms.size());
	for (int i = 0; i < p_uniforms.size(); i++) {
		Ref<RDUniform> uniform = p_uniforms[i];
		ERR_FAIL_COND_V(uniform.is_null(), RID());
		uniforms.write[i] = uniform->base;
	}
	return uniform_set_create(uniforms, p_shader, p_shader_set);
}

Error RenderingDevice::_buffer_update_bind(RID p_buffer, uint32_t p_offset, uint32_t p_size, const Vector<uint8_t> &p_data) {
	return buffer_update(p_buffer, p_offset, p_size, p_data.ptr());
}

static Vector<RenderingDevice::PipelineSpecializationConstant> _get_spec_constants(const TypedArray<RDPipelineSpecializationConstant> &p_constants) {
	Vector<RenderingDevice::PipelineSpecializationConstant> ret;
	ret.resize(p_constants.size());
	for (int i = 0; i < p_constants.size(); i++) {
		Ref<RDPipelineSpecializationConstant> c = p_constants[i];
		ERR_CONTINUE(c.is_null());
		RenderingDevice::PipelineSpecializationConstant &sc = ret.write[i];
		Variant value = c->get_value();
		switch (value.get_type()) {
			case Variant::BOOL: {
				sc.type = RD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_BOOL;
				sc.bool_value = value;
			} break;
			case Variant::INT: {
				sc.type = RD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_INT;
				sc.int_value = value;
			} break;
			case Variant::FLOAT: {
				sc.type = RD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_FLOAT;
				sc.float_value = value;
			} break;
			default: {
			}
		}

		sc.constant_id = c->get_constant_id();
	}
	return ret;
}

RID RenderingDevice::_render_pipeline_create(RID p_shader, FramebufferFormatID p_framebuffer_format, VertexFormatID p_vertex_format, RenderPrimitive p_render_primitive, const Ref<RDPipelineRasterizationState> &p_rasterization_state, const Ref<RDPipelineMultisampleState> &p_multisample_state, const Ref<RDPipelineDepthStencilState> &p_depth_stencil_state, const Ref<RDPipelineColorBlendState> &p_blend_state, BitField<PipelineDynamicStateFlags> p_dynamic_state_flags, uint32_t p_for_render_pass, const TypedArray<RDPipelineSpecializationConstant> &p_specialization_constants) {
	PipelineRasterizationState rasterization_state;
	if (p_rasterization_state.is_valid()) {
		rasterization_state = p_rasterization_state->base;
	}

	PipelineMultisampleState multisample_state;
	if (p_multisample_state.is_valid()) {
		multisample_state = p_multisample_state->base;
		for (int i = 0; i < p_multisample_state->sample_masks.size(); i++) {
			int64_t mask = p_multisample_state->sample_masks[i];
			multisample_state.sample_mask.push_back(mask);
		}
	}

	PipelineDepthStencilState depth_stencil_state;
	if (p_depth_stencil_state.is_valid()) {
		depth_stencil_state = p_depth_stencil_state->base;
	}

	PipelineColorBlendState color_blend_state;
	if (p_blend_state.is_valid()) {
		color_blend_state = p_blend_state->base;
		for (int i = 0; i < p_blend_state->attachments.size(); i++) {
			Ref<RDPipelineColorBlendStateAttachment> attachment = p_blend_state->attachments[i];
			if (attachment.is_valid()) {
				color_blend_state.attachments.push_back(attachment->base);
			}
		}
	}

	return render_pipeline_create(p_shader, p_framebuffer_format, p_vertex_format, p_render_primitive, rasterization_state, multisample_state, depth_stencil_state, color_blend_state, p_dynamic_state_flags, p_for_render_pass, _get_spec_constants(p_specialization_constants));
}

RID RenderingDevice::_compute_pipeline_create(RID p_shader, const TypedArray<RDPipelineSpecializationConstant> &p_specialization_constants = TypedArray<RDPipelineSpecializationConstant>()) {
	return compute_pipeline_create(p_shader, _get_spec_constants(p_specialization_constants));
}

#ifndef DISABLE_DEPRECATED
Vector<int64_t> RenderingDevice::_draw_list_begin_split(RID p_framebuffer, uint32_t p_splits, InitialAction p_initial_color_action, FinalAction p_final_color_action, InitialAction p_initial_depth_action, FinalAction p_final_depth_action, const Vector<Color> &p_clear_color_values, float p_clear_depth, uint32_t p_clear_stencil, const Rect2 &p_region, const TypedArray<RID> &p_storage_textures) {
	ERR_FAIL_V_MSG(Vector<int64_t>(), "Deprecated. Split draw lists are used automatically by RenderingDevice.");
}

Vector<int64_t> RenderingDevice::_draw_list_switch_to_next_pass_split(uint32_t p_splits) {
	ERR_FAIL_V_MSG(Vector<int64_t>(), "Deprecated. Split draw lists are used automatically by RenderingDevice.");
}
#endif

void RenderingDevice::_draw_list_set_push_constant(DrawListID p_list, const Vector<uint8_t> &p_data, uint32_t p_data_size) {
	ERR_FAIL_COND(p_data_size > (uint32_t)p_data.size());
	draw_list_set_push_constant(p_list, p_data.ptr(), p_data_size);
}

void RenderingDevice::_compute_list_set_push_constant(ComputeListID p_list, const Vector<uint8_t> &p_data, uint32_t p_data_size) {
	ERR_FAIL_COND(p_data_size > (uint32_t)p_data.size());
	compute_list_set_push_constant(p_list, p_data.ptr(), p_data_size);
}

static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_NONE, RDG::RESOURCE_USAGE_NONE));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_COPY_FROM, RDG::RESOURCE_USAGE_COPY_FROM));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_COPY_TO, RDG::RESOURCE_USAGE_COPY_TO));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_RESOLVE_FROM, RDG::RESOURCE_USAGE_RESOLVE_FROM));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_RESOLVE_TO, RDG::RESOURCE_USAGE_RESOLVE_TO));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_UNIFORM_BUFFER_READ, RDG::RESOURCE_USAGE_UNIFORM_BUFFER_READ));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_INDIRECT_BUFFER_READ, RDG::RESOURCE_USAGE_INDIRECT_BUFFER_READ));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_TEXTURE_BUFFER_READ, RDG::RESOURCE_USAGE_TEXTURE_BUFFER_READ));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_TEXTURE_BUFFER_READ_WRITE, RDG::RESOURCE_USAGE_TEXTURE_BUFFER_READ_WRITE));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_STORAGE_BUFFER_READ, RDG::RESOURCE_USAGE_STORAGE_BUFFER_READ));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_STORAGE_BUFFER_READ_WRITE, RDG::RESOURCE_USAGE_STORAGE_BUFFER_READ_WRITE));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_VERTEX_BUFFER_READ, RDG::RESOURCE_USAGE_VERTEX_BUFFER_READ));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_INDEX_BUFFER_READ, RDG::RESOURCE_USAGE_INDEX_BUFFER_READ));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_TEXTURE_SAMPLE, RDG::RESOURCE_USAGE_TEXTURE_SAMPLE));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_STORAGE_IMAGE_READ, RDG::RESOURCE_USAGE_STORAGE_IMAGE_READ));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_STORAGE_IMAGE_READ_WRITE, RDG::RESOURCE_USAGE_STORAGE_IMAGE_READ_WRITE));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_ATTACHMENT_COLOR_READ_WRITE, RDG::RESOURCE_USAGE_ATTACHMENT_COLOR_READ_WRITE));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_ATTACHMENT_DEPTH_STENCIL_READ_WRITE, RDG::RESOURCE_USAGE_ATTACHMENT_DEPTH_STENCIL_READ_WRITE));
static_assert(ENUM_MEMBERS_EQUAL(RD::CALLBACK_RESOURCE_USAGE_MAX, RDG::RESOURCE_USAGE_MAX));

void RenderingDevice::save_texture_to_file(RID p_texture, uint32_t p_layer, const TextureFormat &p_format, Size2i p_size, String p_path) {
	if (p_size.x == 0 || p_size.y == 0) {
		OS::get_singleton()->print("p_size.x == 0 || p_size.y == 0\n");
		return;
	}

	OS::get_singleton()->print("p_size = (%d, %d)\n", p_size.x, p_size.y); // 添加换行符以更好地格式化
	
	switch (p_format.format) {
	case DATA_FORMAT_R16G16B16_SFLOAT:
	case DATA_FORMAT_R16G16B16A16_SFLOAT:
	{
		PackedByteArray data_raw = texture_get_data(p_texture, p_layer);
		if (data_raw.size() == 0)
		{
			OS::get_singleton()->print("data_raw.size() = %d\n", data_raw.size());
			return;
		}

		OS::get_singleton()->print("data_raw.size() = %d\n", data_raw.size());

		// 从GPU中获取的数据会有以后最后的对齐位置，所以data_raw是8字节对齐
		const uint16_t *half_ptr = reinterpret_cast<const uint16_t *>(&data_raw[0]);

		size_t pixel_count = p_size.x * p_size.y;
		Ref<Image> img = Image::create_empty(p_size.x, p_size.y, false, Image::FORMAT_RGBA8);
		for (size_t i = 0; i < pixel_count; ++i) {
			// 取半精度值
			uint16_t hR = half_ptr[i * 4 + 0];
			uint16_t hG = half_ptr[i * 4 + 1];
			uint16_t hB = half_ptr[i * 4 + 2];
			// 半精度转 float （Godot 提供的工具函数）
			float fR = Math::half_to_float(hR);
			float fG = Math::half_to_float(hG);
			float fB = Math::half_to_float(hB);
			// clamp 到 [0,1] 并写入 Image
			Color c = Color(CLAMP(fR, 0.0f, 1.0f),
					CLAMP(fG, 0.0f, 1.0f),
					CLAMP(fB, 0.0f, 1.0f));
			int x = int(i % p_size.x);
			int y = int(i / p_size.x);
			img->set_pixel(x, y, c);
		}
		img->save_png(p_path);
		CharString u8 = p_path.utf8();
		OS::get_singleton()->print("Save file to {%s} success!\n", u8.get_data());
	}
	break;
	case DATA_FORMAT_D32_SFLOAT_S8_UINT:
	{
		// 这里事实上只会有4字节的深度值出来，模板值不会有。
		PackedByteArray data_raw = texture_get_data(p_texture, p_layer);
		if (data_raw.size() == 0)
		{
			OS::get_singleton()->print("data_raw.size() = %d\n", data_raw.size());
			return;
		}

		OS::get_singleton()->print("data_raw.size() = %d\n", data_raw.size());

		const size_t w = p_size.x, h = p_size.y;
		const size_t n = w * h;
		const size_t total = data_raw.size();

		Ref<Image> img = Image::create_empty(p_size.x, p_size.y, false, Image::FORMAT_RGBA8);
		Ref<Image> stencil_img = Image::create_empty(p_size.x, p_size.y, false, Image::FORMAT_RGBA8);

		auto put_pixel = [&](size_t i, float d, uint8_t st) {
			// 可选：把深度可视化成 0~1（必要时反转或夹紧）
			float v = Math::is_finite(d) ? CLAMP(d, 0.0f, 1.0f) : 0.0f;
			int x = int(i % w);
			int y = int(i / w);
			if (v > 0.f)
				img->set_pixel(x, y, Color(0.f, 1.f, 0.f, 1.0f));
			else
				img->set_pixel(x, y, Color(0.f, 0.f, 0.f, 1.0f));

			float s = st / 255.0f;
			stencil_img->set_pixel(x, y, Color(s, s, s, 1.0f));
		};

		if (total == n * 5) {
			// 逐像素交错：4 字节深度 + 1 字节模板
			// [d0, d1, d2, d3, S]
			for (size_t i = 0; i < n; ++i) {
				size_t base = i * 4;
				float d;
				std::memcpy(&d, &data_raw[base + 0], 4);
				uint8_t st = data_raw[n * 4 + i];
				put_pixel(i, d, st);
			}
		}

		img->save_png(p_path);
		stencil_img->save_png("user://stencil.png");

		//if (1) {
		//	PackedByteArray data_raw = stencil_get_data(p_texture, p_layer);
		//	if (data_raw.size() == 0)
		//	{
		//		OS::get_singleton()->print("data_raw.size() = %d\n", data_raw.size());
		//		return;
		//	}

		//	const size_t w = p_size.x, h = p_size.y;
		//	const size_t n = w * h;
		//	const size_t total = data_raw.size();

		//	Ref<Image> img = Image::create_empty(p_size.x, p_size.y, false, Image::FORMAT_RGBA8);

		//	if (true) {
		//		int line = -1;
		//		// 模板：只有1个字节的数据。
		//		for (size_t i = 0; i < n; ++i) {
		//			uint8_t v0 = data_raw[i];
		//			//uint8_t v1 = data_raw[i * 4 + 1];
		//			//uint8_t v2 = data_raw[i * 4 + 2];
		//			//uint8_t v3 = data_raw[i * 4 + 3];
		//			//float v = Math::is_finite(ptr[i]) ? CLAMP(ptr[i], 0.0f, 1.0f) : 0.0f;
		//			int x = int(i % w);
		//			int y = int(i / w);
		//			//img->set_pixel(x, y, Color(v0 / 255.f, v1 / 255.f, v2 / 255.f, 1.0f));
		//			img->set_pixel(x, y, Color(v0 / 255.f, 0, 0, 1.0f));

		//			//if (v > 0.f) {
		//			//	if (line == y || line == -1) {
		//			//		String log = vformat("%f,", ptr[i]);
		//			//		RSG::write_log_to_file(log, false, false);
		//			//	}
		//			//	else {
		//			//		line = y;
		//			//		String log = vformat("%f", ptr[i]);
		//			//		RSG::write_log_to_file(log, false, true);
		//			//	}
		//			//}

		//			//v *= 1000.f;
		//			//if (v > 0.1f)
		//			//	img->set_pixel(x, y, Color(1.f, 0, 0, 1.0f));
		//			//else
		//			//	img->set_pixel(x, y, Color(0.f, 0.f, 0.f, 1.f));
		//			//img->set_pixel(x, y, Color(v0 / 255.f, 0, 0, 1.0f));
		//			//img1->set_pixel(x, y, Color(0, v1 / 255.f, 0, 1.0f));
		//			//img2->set_pixel(x, y, Color(0, 0, v2 / 255.f, 1.0f));
		//			//img3->set_pixel(x, y, Color(0, 0, v3 / 255.f, 1.0f));

		//			//float s = Math::is_finite(ptr[i])? CLAMP(ptr[i + n], 0.0f, 1.0f) : 0.0f;
		//			//uint8_t v4 = data_raw[i * 4 + 4];
		//			//uint8_t v5 = data_raw[i * 4 + 5];
		//			//uint8_t v6 = data_raw[i * 4 + 6];
		//			//uint8_t v7 = data_raw[i * 4 + 7];
		//			//stencil_img->set_pixel(x, y, Color(v4 / 255.f, v5 / 255.f, v6 / 255.f, 1.0f));
		//		}

		//	}

		//	img->save_png(p_path);
		//	//stencil_img->save_png("user://stencil.png");
		//	//img1->save_png("user://img1.png");
		//	//img2->save_png("user://img2.png");
		//	//img3->save_png("user://img3.png");

		//	CharString u8 = p_path.utf8();
		//	OS::get_singleton()->print("Save file to {%s} success!\n", u8.get_data());
		//	//OS::get_singleton()->print("Save stencil to {user://stencil.png} success!\n");
		//}
		//else {
		//PackedByteArray data_raw = depth_get_data(p_texture, p_layer);
		//if (data_raw.size() == 0)
		//{
		//	OS::get_singleton()->print("data_raw.size() = %d\n", data_raw.size());
		//	return;
		//}

		//const size_t w = p_size.x, h = p_size.y;
		//const size_t n = w * h;
		//const size_t total = data_raw.size();

		//Ref<Image> img = Image::create_empty(p_size.x, p_size.y, false, Image::FORMAT_RGBA8);
		////Ref<Image> stencil_img = Image::create_empty(p_size.x, p_size.y, false, Image::FORMAT_RGBA8);
		////Ref<Image> img1 = Image::create_empty(p_size.x, p_size.y, false, Image::FORMAT_RGBA8);
		////Ref<Image> img2 = Image::create_empty(p_size.x, p_size.y, false, Image::FORMAT_RGBA8);
		////Ref<Image> img3 = Image::create_empty(p_size.x, p_size.y, false, Image::FORMAT_RGBA8);

		//auto put_pixel = [&](size_t i, float d, uint8_t st) {
		//	// 可选：把深度可视化成 0~1（必要时反转或夹紧）
		//	float v = Math::is_finite(d) ? CLAMP(d, 0.0f, 1.0f) : 0.0f;
		//	int x = int(i % w);
		//	int y = int(i / w);
		//	img->set_pixel(x, y, Color(v, v, v, 1.0f));

		//	float s = st / 255.0f;
		//	//stencil_img->set_pixel(x, y, Color(s, s, s, 1.0f));
		//};

		//// —— 探测内存顺序：DS(Depth 后 Stencil) 还是 SD(Stencil 在前 Depth 在后)
		////auto score_stencil = [&](bool assume_SD)->size_t {
		////	size_t hit = 0, sample = MIN<size_t>(n, 50000);
		////	for (size_t i = 0; i < sample; ++i) {
		////		size_t base = i * 5;
		////		uint8_t st = assume_SD ? data_raw[base + 0] : data_raw[base + 4];
		////		if (st == 0 || st == 255) ++hit;  // 常见模板值
		////	}
		////	return hit;
		////};
		////bool use_SD = score_stencil(true) > score_stencil(false); // 命中多者更像模板
		//if (true){//total == n * 8) {
		//	// 测试用，先输出深度值看看
		//	const float* ptr = reinterpret_cast<const float *>(&data_raw[0]);

		//	int line = -1;
		//	for (size_t i = 0; i < n; ++i) {
		//		//uint8_t v0 = data_raw[i * 4 + 0];
		//		//uint8_t v1 = data_raw[i * 4 + 1];
		//		//uint8_t v2 = data_raw[i * 4 + 2];
		//		//uint8_t v3 = data_raw[i * 4 + 3];
		//		float v = Math::is_finite(ptr[i]) ? CLAMP(ptr[i], 0.0f, 1.0f) : 0.0f;
		//		int x = int(i % w);
		//		int y = int(i / w);

		//		if (v > 0.f) {
		//			if (line == y || line == -1) {
		//				String log = vformat("%f,", ptr[i]);
		//				RSG::write_log_to_file(log, false, false);
		//			}
		//			else{
		//				line = y;
		//				String log = vformat("%f", ptr[i]);
		//				RSG::write_log_to_file(log, false, true);
		//			}
		//		}

		//		v *= 1000.f;
		//		if (v > 0.1f)
		//			img->set_pixel(x, y, Color(1.f, 0, 0, 1.0f));
		//		else
		//			img->set_pixel(x, y, Color(0.f, 0.f, 0.f, 1.f));
		//		//img->set_pixel(x, y, Color(v0 / 255.f, 0, 0, 1.0f));
		//		//img1->set_pixel(x, y, Color(0, v1 / 255.f, 0, 1.0f));
		//		//img2->set_pixel(x, y, Color(0, 0, v2 / 255.f, 1.0f));
		//		//img3->set_pixel(x, y, Color(0, 0, v3 / 255.f, 1.0f));

		//		//float s = Math::is_finite(ptr[i])? CLAMP(ptr[i + n], 0.0f, 1.0f) : 0.0f;
		//		//uint8_t v4 = data_raw[i * 4 + 4];
		//		//uint8_t v5 = data_raw[i * 4 + 5];
		//		//uint8_t v6 = data_raw[i * 4 + 6];
		//		//uint8_t v7 = data_raw[i * 4 + 7];
		//		//stencil_img->set_pixel(x, y, Color(v4 / 255.f, v5 / 255.f, v6 / 255.f, 1.0f));
		//	}

		//}
		//else if (total == n * 5) {
		//	//if (use_SD) {
		//		// 逐像素交错：1字节模板+4字节深度
		//		// [S, d0, d1, d2, d3]
		//		for (size_t i = 0; i < n; ++i) {
		//			size_t base = i * 5;
		//			uint8_t st = data_raw[base + 0];
		//			float d; std::memcpy(&d, &data_raw[base + 1], 4);
		//			put_pixel(i, d, st);
		//		}
		//	//}
		//	//else {
		//	//	// 逐像素交错：4 字节深度 + 1 字节模板
		//	//	// [d0, d1, d2, d3, S]
		//	//	for (size_t i = 0; i < n; ++i) {
		//	//		size_t base = i * 5;
		//	//		float d;
		//	//		std::memcpy(&d, &data_raw[base + 0], 4);
		//	//		uint8_t st = data_raw[base + 4];
		//	//		put_pixel(i, d, st);
		//	//	}
		//	//}
		//} else if (total == n * 8) {
		//	// 逐像素 8 字节对齐：4 字节深度 + 1 字节模板 + 3 字节填充
		//	for (size_t i = 0; i < n; ++i) {
		//		size_t base = i * 8;
		//		float d;
		//		std::memcpy(&d, &data_raw[base + 0], 4);
		//		uint8_t st = data_raw[base + 4];
		//		put_pixel(i, d, st);
		//	}
		//} else if (total == n * 4 + n) {
		//	// 平面分离：先所有深度，再所有模板（少见，但做个兜底）
		//	size_t st_off = n * 4;
		//	for (size_t i = 0; i < n; ++i) {
		//		float d;
		//		std::memcpy(&d, &data_raw[i * 4], 4);
		//		uint8_t st = data_raw[st_off + i];
		//		put_pixel(i, d, st);
		//	}
		//} else {
		//	// 非常规：可能存在逐行对齐（row pitch）。推断每行步长并按交错读取。
		//	size_t row_stride = total / h; // 约分得到每行字节数
		//	bool interleaved5 = row_stride >= w * 5; // 简单判定
		//	bool interleaved8 = row_stride >= w * 8;

		//	if (interleaved5 || interleaved8) {
		//		size_t px = interleaved8 ? 8 : 5;
		//		for (size_t y = 0; y < h; ++y) {
		//			size_t row_base = y * row_stride;
		//			for (size_t x = 0; x < w; ++x) {
		//				size_t base = row_base + x * px;
		//				float d;
		//				std::memcpy(&d, &data_raw[base + 0], 4);
		//				uint8_t st = data_raw[base + 4];
		//				put_pixel(y * w + x, d, st);
		//			}
		//		}
		//	} else {
		//		OS::get_singleton()->print("Unexpected DS layout: bytes=%zu\n", total);
		//	}
		//}

		//img->save_png(p_path);
		////stencil_img->save_png("user://stencil.png");
		////img1->save_png("user://img1.png");
		////img2->save_png("user://img2.png");
		////img3->save_png("user://img3.png");

		//CharString u8 = p_path.utf8();
		//OS::get_singleton()->print("Save file to {%s} success!\n", u8.get_data());
		////OS::get_singleton()->print("Save stencil to {user://stencil.png} success!\n");
		//
		//}

	}
	break;
	default :
	{
		OS::get_singleton()->print("p_format.format = %d, save failed!\n", p_format.format);
	}
	break;
	}
}
