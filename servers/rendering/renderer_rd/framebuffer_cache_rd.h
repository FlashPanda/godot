/**************************************************************************/
/*  framebuffer_cache_rd.h                                                */
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

#ifndef FRAMEBUFFER_CACHE_RD_H
#define FRAMEBUFFER_CACHE_RD_H

#include "core/templates/local_vector.h"
#include "core/templates/paged_allocator.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_device_binds.h"

// 帧缓冲的缓存池
// 它根据一组输入纹理（attachments）、可选的多通道绘制流程
// （multipass 的 FramebufferPass 描述）以及视图数量（multiview），
// 计算哈希并复用已创建过的 RD 帧缓冲对象（RID），
// 避免频繁创建/销毁导致的 CPU/GPU 开销；同时注册失效回调，
// 保证当底层帧缓冲被 RD 释放时，从缓存里把对应条目移除。
class FramebufferCacheRD : public Object {
	GDCLASS(FramebufferCacheRD, Object)

	// 内部缓存条目，记录一个 framebuffer 及其关键构成
	struct Cache {
		Cache *prev = nullptr;	// 双向链表前驱（用于哈希桶内的冲突链）
		Cache *next = nullptr;	// 双向链表后继
		uint32_t hash = 0;		// 此条目的哈希值（用于快速匹配）
		RID cache;				// RD 层创建的 framebuffer 资源句柄
		LocalVector<RID> textures;		// 本 framebuffer 绑定的纹理附件列表（color/depth/stencil 等通过索引关联）
		LocalVector<RD::FramebufferPass> passes;	 // multipass 描述（各 pass 的附件引用）
		uint32_t views = 0;		// multiview 数量（如 VR/视图数组）
	};

	PagedAllocator<Cache> cache_allocator;		// 分页分配器：高效批量分配/释放 Cache 节点，降低碎片

	enum {
		HASH_TABLE_SIZE = 16381 // Prime		// 哈希表大小（质数，减少碰撞）
	};

	Cache *hash_table[HASH_TABLE_SIZE] = {};	// 哈希桶数组，存放链表头指针，初始化为全 nullptr

	// 计算单个 pass 的哈希并与累积哈希混合
	static _FORCE_INLINE_ uint32_t _hash_pass(const RD::FramebufferPass &p, uint32_t h) {
		h = hash_murmur3_one_32(p.depth_attachment, h);		// 混合深度附件 ID/索引
		h = hash_murmur3_one_32(p.vrs_attachment, h);		// 混合 VRS（可变分辨率着色）附件

		h = hash_murmur3_one_32(p.color_attachments.size(), h);		// 混合颜色附件数量
		for (int i = 0; i < p.color_attachments.size(); i++) {		// 逐个混合颜色附件
			h = hash_murmur3_one_32(p.color_attachments[i], h);
		}

		h = hash_murmur3_one_32(p.resolve_attachments.size(), h);	// 混合 resolve（MSAA 解析）附件数量
		for (int i = 0; i < p.resolve_attachments.size(); i++) {	// 逐个混合 resolve 附件
			h = hash_murmur3_one_32(p.resolve_attachments[i], h);
		}

		h = hash_murmur3_one_32(p.preserve_attachments.size(), h);	// 混合 preserve 附件数量（保留的附件）
		for (int i = 0; i < p.preserve_attachments.size(); i++) {	// 逐个混合 preserve 附件
			h = hash_murmur3_one_32(p.preserve_attachments[i], h);
		}

		return h;	// 返回累积后的哈希
	}

	// 精确比较两个 pass 是否等价
	static _FORCE_INLINE_ bool _compare_pass(const RD::FramebufferPass &a, const RD::FramebufferPass &b) {
		if (a.depth_attachment != b.depth_attachment) {
			return false;
		}

		if (a.vrs_attachment != b.vrs_attachment) {
			return false;
		}

		if (a.color_attachments.size() != b.color_attachments.size()) {
			return false;
		}

		for (int i = 0; i < a.color_attachments.size(); i++) {
			if (a.color_attachments[i] != b.color_attachments[i]) {
				return false;
			}
		}

		if (a.resolve_attachments.size() != b.resolve_attachments.size()) {
			return false;
		}

		for (int i = 0; i < a.resolve_attachments.size(); i++) {
			if (a.resolve_attachments[i] != b.resolve_attachments[i]) {
				return false;
			}
		}

		if (a.preserve_attachments.size() != b.preserve_attachments.size()) {
			return false;
		}

		for (int i = 0; i < a.preserve_attachments.size(); i++) {
			if (a.preserve_attachments[i] != b.preserve_attachments[i]) {
				return false;
			}
		}

		return true;
	}

	// 终止递归重载：只混合一个 RID
	_FORCE_INLINE_ uint32_t _hash_rids(uint32_t h, const RID &arg) {
		return hash_murmur3_one_64(arg.get_id(), h);
	}

	// 递归模板：混合多个 RID
	template <typename... Args>
	uint32_t _hash_rids(uint32_t h, const RID &arg, Args... args) {
		h = hash_murmur3_one_64(arg.get_id(), h);	 // 先混合当前 RID
		return _hash_rids(h, args...);	// 递归处理其余参数
	}

	_FORCE_INLINE_ bool _compare_args(uint32_t idx, const LocalVector<RID> &textures, const RID &arg) {
		return textures[idx] == arg;
	}

	// 递归比较多个参数
	template <typename... Args>
	_FORCE_INLINE_ bool _compare_args(uint32_t idx, const LocalVector<RID> &textures, const RID &arg, Args... args) {
		if (textures[idx] != arg) {
			return false;
		}
		return _compare_args(idx + 1, textures, args...);
	}

	_FORCE_INLINE_ void _create_args(Vector<RID> &textures, const RID &arg) {
		textures.push_back(arg);
	}

	// 递归构造纹理数组
	template <typename... Args>
	_FORCE_INLINE_ void _create_args(Vector<RID> &textures, const RID &arg, Args... args) {
		textures.push_back(arg);
		_create_args(textures, args...);
	}

	static FramebufferCacheRD *singleton;		// 单例指针，便于全局访问（渲染器侧一般用单例）

	uint32_t cache_instances_used = 0;		// 已创建并缓存的 framebuffer 数量统计（调试/统计用途）

	void _invalidate(Cache *p_cache);		// 使缓存条目失效（从哈希表移除并释放资源）
	static void _framebuffer_invalidation_callback(void *p_userdata);	// RD 通知回调：当底层 framebuffer 失效时调用，p_userdata 指向 Cache

	// 内部分配与落库
	RID _allocate_from_data(uint32_t p_views, uint32_t p_hash, uint32_t p_table_idx, const Vector<RID> &p_textures, const Vector<RD::FramebufferPass> &p_passes) {
		RID rid;	 // 将要创建的 framebuffer 句柄
		if (p_passes.size()) {	// 如果有 multipass 描述
			rid = RD::get_singleton()->framebuffer_create_multipass(p_textures, p_passes, RD::INVALID_ID, p_views);
		} else {
			rid = RD::get_singleton()->framebuffer_create(p_textures, RD::INVALID_ID, p_views);
		}

		ERR_FAIL_COND_V(rid.is_null(), rid);

		Cache *c = cache_allocator.alloc();		// 向分页分配器申请一个 Cache 节点
		c->views = p_views;		// 视图数量
		c->cache = rid;			// 记录RD资源句柄
		c->hash = p_hash;		// 记录哈希值
		c->textures.resize(p_textures.size());		// 拷贝纹理附件列表
		for (uint32_t i = 0; i < c->textures.size(); i++) {
			c->textures[i] = p_textures[i];
		}
		c->passes.resize(p_passes.size());		// 拷贝 multipass 描述
		for (uint32_t i = 0; i < c->passes.size(); i++) {
			c->passes[i] = p_passes[i];
		}
		c->prev = nullptr;		// 插入到哈希桶链表头部
		c->next = hash_table[p_table_idx];
		if (hash_table[p_table_idx]) {
			hash_table[p_table_idx]->prev = c;
		}
		hash_table[p_table_idx] = c;		// 更新桶头指针

		RD::get_singleton()->framebuffer_set_invalidation_callback(rid, _framebuffer_invalidation_callback, c);

		cache_instances_used++;		// 计数

		return rid;		// 返回句柄
	}

private:
	static void _bind_methods();

public:
	template <typename... Args>
	RID get_cache(Args... args) {		// 获取（或创建）单视图、零 pass 的 framebuffer：参数是若干个附件 RID
		uint32_t h = hash_murmur3_one_32(1); //1 view	// 初始哈希：写入视图数（固定 1）
		h = hash_murmur3_one_32(sizeof...(Args), h);	// 混合附件数量
		h = _hash_rids(h, args...);						// 递归混合所有 RID（64 位 ID）
		h = hash_murmur3_one_32(0, h); // 0 passes		// 混合 pass 数量（0）
		h = hash_fmix32(h);								// 尾部混合（murmur3 fmix），提高分布性

		uint32_t table_idx = h % HASH_TABLE_SIZE;		// 哈希桶索引
		{
			const Cache *c = hash_table[table_idx];		// 遍历桶

			while (c) {
				// 匹配多视图条件
				if (c->hash == h && c->passes.size() == 0 && c->textures.size() == sizeof...(Args) && c->views == 1 && _compare_args(0, c->textures, args...)) {
					return c->cache;		// 匹配成功
				}
				c = c->next;
			}
		}

		// Not in cache, create:

		Vector<RID> textures;				// 未命中则创建
		_create_args(textures, args...);

		return _allocate_from_data(1, h, table_idx, textures, Vector<RD::FramebufferPass>());	// 创建并缓存
	}

	// 获取（或创建）多视图、零 pass 的 framebuffer
	template <typename... Args>
	RID get_cache_multiview(uint32_t p_views, Args... args) {
		uint32_t h = hash_murmur3_one_32(p_views);		// 初始哈希：写入视图数
		h = hash_murmur3_one_32(sizeof...(Args), h);	// 混合附件数量
		h = _hash_rids(h, args...);						// 混合所有附件 RID
		h = hash_murmur3_one_32(0, h); // 0 passes		// 混合 pass 数量（0）
		h = hash_fmix32(h);								// fmix

		uint32_t table_idx = h % HASH_TABLE_SIZE;		// 哈希桶索引
		{
			const Cache *c = hash_table[table_idx];		// 遍历桶

			while (c) {
				// 匹配多视图条件
				if (c->hash == h && c->passes.size() == 0 && c->textures.size() == sizeof...(Args) && c->views == p_views && _compare_args(0, c->textures, args...)) {
					return c->cache;		// 缓存命中
				}
				c = c->next;
			}
		}

		// Not in cache, create:

		Vector<RID> textures;				// 未命中则创建
		_create_args(textures, args...);

		return _allocate_from_data(p_views, h, table_idx, textures, Vector<RD::FramebufferPass>());		// 创建并缓存
	}

	// 获取（或创建）多 pass / 可多视图的 framebuffer
	RID get_cache_multipass(const Vector<RID> &p_textures, const Vector<RD::FramebufferPass> &p_passes, uint32_t p_views = 1) {
		uint32_t h = hash_murmur3_one_32(p_views);					// 混合视图数
		h = hash_murmur3_one_32(p_textures.size(), h);				// 混合附件数量
		for (int i = 0; i < p_textures.size(); i++) {
			h = hash_murmur3_one_64(p_textures[i].get_id(), h);		// 逐个混合纹理 RID（64 位）
		}
		h = hash_murmur3_one_32(p_passes.size(), h);				// 混合 pass 数量
		for (int i = 0; i < p_passes.size(); i++) {
			h = _hash_pass(p_passes[i], h);							// 逐个混合每个 pass 的内容
		}

		h = hash_fmix32(h);											// fmix

		uint32_t table_idx = h % HASH_TABLE_SIZE;					// 哈希桶索引
		{
			const Cache *c = hash_table[table_idx];

			while (c) {
				// 先比粗略条件减少代价
				if (c->hash == h && c->views == p_views && c->textures.size() == (uint32_t)p_textures.size() && c->passes.size() == (uint32_t)p_passes.size()) {
					bool all_ok = true;

					for (int i = 0; i < p_textures.size(); i++) {		// 精细比较纹理顺序与值
						if (p_textures[i] != c->textures[i]) {
							all_ok = false;
							break;
						}
					}

					if (all_ok) {
						for (int i = 0; i < p_passes.size(); i++) {
							if (!_compare_pass(p_passes[i], c->passes[i])) {		// 再逐个比较 pass（含所有附件数组）
								all_ok = false;
								break;
							}
						}
					}

					if (all_ok) {		// 全部匹配 → 返回缓存
						return c->cache;
					}
				}
				c = c->next;
			}
		}

		// Not in cache, create:
		return _allocate_from_data(p_views, h, table_idx, p_textures, p_passes);		// 未命中则创建并缓存
	}

	// 脚本友好的重载（TypedArray）
	static RID get_cache_multipass_array(const TypedArray<RID> &p_textures, const TypedArray<RDFramebufferPass> &p_passes, uint32_t p_views = 1);

	// 单例获取
	static FramebufferCacheRD *get_singleton() { return singleton; }

	FramebufferCacheRD();
	~FramebufferCacheRD();
};

#endif // FRAMEBUFFER_CACHE_RD_H
