/**************************************************************************/
/*  compositor.h                                                          */
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

#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "core/io/resource.h"
#include "core/object/gdvirtual.gen.inc"
#include "servers/rendering/storage/render_data.h"

/* Compositor Effect */

class CompositorEffect : public Resource {
	GDCLASS(CompositorEffect, Resource);

public:
	enum EffectCallbackType {
		EFFECT_CALLBACK_TYPE_PRE_OPAQUE,
		EFFECT_CALLBACK_TYPE_POST_OPAQUE,
		EFFECT_CALLBACK_TYPE_POST_SKY,
		EFFECT_CALLBACK_TYPE_PRE_TRANSPARENT,
		EFFECT_CALLBACK_TYPE_POST_TRANSPARENT,
		EFFECT_CALLBACK_TYPE_MAX
	};

private:
	RID rid;
	bool enabled = true;
	EffectCallbackType effect_callback_type = EFFECT_CALLBACK_TYPE_POST_TRANSPARENT;

	bool access_resolved_color = false;
	bool access_resolved_depth = false;
	bool needs_motion_vectors = false;
	bool needs_normal_roughness = false;
	bool needs_separate_specular = false;

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

	void _call_render_callback(int p_effect_callback_type, const RenderData *p_render_data);

	// GDVIRTUAL2(_render_callback, int, const RenderData *)

	StringName _gdvirtual__render_callback_sn = "_render_callback";
	mutable bool _gdvirtual__render_callback_initialized = false;
	mutable void *_gdvirtual__render_callback = nullptr;

	_FORCE_INLINE_ bool _gdvirtual__render_callback_call(int arg1, const RenderData *arg2) {
		ScriptInstance *_script_instance = ((Object *)(this))->get_script_instance();
		if (_script_instance) {
			Callable::CallError ce;
			Variant vargs[2] = { _to_variant(arg1), _to_variant(arg2) };
			const Variant *vargptrs[2] = { &vargs[0], &vargs[1] };
			_script_instance->callp(_gdvirtual__render_callback_sn, (const Variant **)vargptrs, 2, ce);
			if (ce.error == Callable::CallError::CALL_OK) {
				return true;
			}
		}
		if (unlikely(_get_extension() && !_gdvirtual__render_callback_initialized)) {
			_gdvirtual__render_callback = nullptr;
			if (_get_extension()->get_virtual_call_data && _get_extension()->call_virtual_with_data) {
				_gdvirtual__render_callback = _get_extension()->get_virtual_call_data(_get_extension()->class_userdata, &_gdvirtual__render_callback_sn);
			} else if (_get_extension()->get_virtual) {
				_gdvirtual__render_callback = (void *)_get_extension()->get_virtual(_get_extension()->class_userdata, &_gdvirtual__render_callback_sn);
			}
			GDVIRTUAL_TRACK(_gdvirtual__render_callback, _gdvirtual__render_callback_initialized);
			_gdvirtual__render_callback_initialized = true;
		}
		if (_gdvirtual__render_callback) {
			PtrToArg<int>::EncodeT argval1 = (PtrToArg<int>::EncodeT)arg1;
			PtrToArg<const RenderData *>::EncodeT argval2 = (PtrToArg<const RenderData *>::EncodeT)arg2;
			GDExtensionConstTypePtr argptrs[2] = { &argval1, &argval2 };
			if (_get_extension()->get_virtual_call_data && _get_extension()->call_virtual_with_data) {
				_get_extension()->call_virtual_with_data(
					_get_extension_instance(),
					&_gdvirtual__render_callback_sn,
					_gdvirtual__render_callback,
					reinterpret_cast<GDExtensionConstTypePtr *>(argptrs),
					nullptr
				);
			} else {
				((GDExtensionClassCallVirtual)_gdvirtual__render_callback)(
					_get_extension_instance(),
					reinterpret_cast<GDExtensionConstTypePtr *>(argptrs),
					nullptr
				);
			}
			return true;
		}
		return false;
	}

	_FORCE_INLINE_ bool _gdvirtual__render_callback_overridden() const {
		ScriptInstance *_script_instance = ((Object *)(this))->get_script_instance();
		if (_script_instance && _script_instance->has_method(_gdvirtual__render_callback_sn)) {
			return true;
		}
		if (unlikely(_get_extension() && !_gdvirtual__render_callback_initialized)) {
			_gdvirtual__render_callback = nullptr;
			if (_get_extension()->get_virtual_call_data && _get_extension()->call_virtual_with_data) {
				_gdvirtual__render_callback = _get_extension()->get_virtual_call_data(_get_extension()->class_userdata, &_gdvirtual__render_callback_sn);
			} else if (_get_extension()->get_virtual) {
				_gdvirtual__render_callback = (void *)_get_extension()->get_virtual(_get_extension()->class_userdata, &_gdvirtual__render_callback_sn);
			}
			GDVIRTUAL_TRACK(_gdvirtual__render_callback, _gdvirtual__render_callback_initialized);
			_gdvirtual__render_callback_initialized = true;
		}
		if (_gdvirtual__render_callback) {
			return true;
		}
		return false;
	}

	_FORCE_INLINE_ static MethodInfo _gdvirtual__render_callback_get_method_info() {
		MethodInfo method_info;
		method_info.name = "_render_callback";
		method_info.flags = METHOD_FLAG_VIRTUAL;
		method_info.arguments.push_back(GetTypeInfo<int>::get_class_info());
		method_info.arguments_metadata.push_back(GetTypeInfo<int>::METADATA);
		method_info.arguments.push_back(GetTypeInfo<const RenderData *>::get_class_info());
		method_info.arguments_metadata.push_back(GetTypeInfo<const RenderData *>::METADATA);
		return method_info;
	}


public:
	virtual RID get_rid() const override { return rid; }

	void set_enabled(bool p_enabled);
	bool get_enabled() const;

	void set_effect_callback_type(EffectCallbackType p_callback_type);
	EffectCallbackType get_effect_callback_type() const;

	void set_access_resolved_color(bool p_enabled);
	bool get_access_resolved_color() const;

	void set_access_resolved_depth(bool p_enabled);
	bool get_access_resolved_depth() const;

	void set_needs_motion_vectors(bool p_enabled);
	bool get_needs_motion_vectors() const;

	void set_needs_normal_roughness(bool p_enabled);
	bool get_needs_normal_roughness() const;

	void set_needs_separate_specular(bool p_enabled);
	bool get_needs_separate_specular() const;

	CompositorEffect();
	~CompositorEffect();
};

VARIANT_ENUM_CAST(CompositorEffect::EffectCallbackType)

/* Compositor */

class Compositor : public Resource {
	GDCLASS(Compositor, Resource);

private:
	RID compositor;

	// Compositor effects
	LocalVector<Ref<CompositorEffect>> effects;

protected:
	static void _bind_methods();

public:
	virtual RID get_rid() const override { return compositor; }

	Compositor();
	~Compositor();

	// Compositor effects
	void set_compositor_effects(const TypedArray<CompositorEffect> &p_compositor_effects);
	TypedArray<CompositorEffect> get_compositor_effects() const;
};

#endif // COMPOSITOR_H
