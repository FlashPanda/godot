//
// Created by chongming on 2025/11/27.
//

#include "register_types.h"
#include "core/object/class_db.h"
#include "my_post_effect.h"

void initialize_my_post_effect_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
		return;

	GDREGISTER_CLASS(MyPostEffect)
}

void uninitialize_my_post_effect_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
		return;
}