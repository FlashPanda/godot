# 子目录 CMakeLists.txt

# 确保上层已定义目标 godot
if(NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Please add this subdirectory after defining the 'godot' target.")
endif()

# ========== 第三方 ufbx 源 ==========
# SCons 中的 '#thirdparty/ufbx/' 指项目根下的 thirdparty/ufbx
set(UFBX_DIR "${CMAKE_SOURCE_DIR}/thirdparty/ufbx")

# 仅包含第三方 ufbx.c，带有其专属编译宏与禁用警告（不污染全局）
add_library(ufbx_thirdparty OBJECT
        "${UFBX_DIR}/ufbx.c"
)

# ufbx 专属头文件路径
target_include_directories(ufbx_thirdparty
        PUBLIC
        "${UFBX_DIR}"
)

# 只对 ufbx 源生效的编译宏（对应 SCsub 中 env_thirdparty.CPPDEFINES）
target_compile_definitions(ufbx_thirdparty
        PRIVATE
        UFBX_NO_SUBDIVISION
        UFBX_NO_TESSELLATION
        UFBX_NO_GEOMETRY_CACHE
        UFBX_NO_SCENE_EVALUATION
        UFBX_NO_INDEX_GENERATION
        UFBX_NO_SKINNING_EVALUATION
        UFBX_NO_FORMAT_OBJ
)

# 仅对 ufbx 源禁用编译警告（对应 env_thirdparty.disable_warnings()）
# 兼容常见编译器
target_compile_options(ufbx_thirdparty
        PRIVATE
        $<$<C_COMPILER_ID:MSVC>:/w>
        $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:-w>
)

# ========== 模块源码收集 ==========
# 与 SCons 的 add_source_files 行为一致：收集当前目录与 structures 子目录
file(GLOB MODULE_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/structures/*.cpp"
)

# 可选：编辑器相关源码（对应 SCons 的 if env.editor_build）
# 由上层设置 GODOT_EDITOR_BUILD=ON 控制；若未设置则默认不添加
if(DEFINED GODOT_EDITOR_BUILD AND GODOT_EDITOR_BUILD)
    file(GLOB MODULE_EDITOR_SOURCES
            CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp"
    )
    list(APPEND MODULE_SOURCES ${MODULE_EDITOR_SOURCES})
endif()

# ========== 连接到 godot 目标 ==========
# 模块源文件并入 godot
target_sources(godot PRIVATE
        ${MODULE_SOURCES}
        # 将第三方对象库的对象并入 godot（确保依赖与重建）
        $<TARGET_OBJECTS:ufbx_thirdparty>
)

# 与 SCons 的 env_fbx.Prepend(CPPPATH=[thirdparty_dir]) 对齐：让模块可见 ufbx 头
target_include_directories(godot PRIVATE "${UFBX_DIR}")

# 显式依赖，确保第三方变化会触发 godot 相关文件重建（与 SCons 的 Depends 对齐）
add_dependencies(godot ufbx_thirdparty)