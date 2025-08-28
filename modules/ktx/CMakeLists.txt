cmake_minimum_required(VERSION 3.20)

# 这些开关用于模拟 SCons 中的条件
option(EDITOR_BUILD "Editor build (mirrors env.editor_build)" OFF)
option(USE_VULKAN "Whether Vulkan is enabled (mirrors env[\"vulkan\"])" ON)

# thirdparty/libktx 路径
set(_KTX_DIR ${CMAKE_SOURCE_DIR}/thirdparty/libktx)

# thirdparty 源文件列表（与 SCsub 对齐）
set(_KTX_THIRDPARTY_SRCS
        ${_KTX_DIR}/lib/basis_transcode.cpp
        ${_KTX_DIR}/lib/checkheader.c
        ${_KTX_DIR}/lib/filestream.c
        ${_KTX_DIR}/lib/hashlist.c
        ${_KTX_DIR}/lib/memstream.c
        ${_KTX_DIR}/lib/miniz_wrapper.cpp
        ${_KTX_DIR}/lib/swap.c
        ${_KTX_DIR}/lib/texture.c
        ${_KTX_DIR}/lib/texture1.c
        ${_KTX_DIR}/lib/texture2.c
        ${_KTX_DIR}/lib/vkformat_check.c
        ${_KTX_DIR}/lib/vkformat_typesize.c
        ${_KTX_DIR}/lib/dfdutils/createdfd.c
        ${_KTX_DIR}/lib/dfdutils/colourspaces.c
        ${_KTX_DIR}/lib/dfdutils/interpretdfd.c
        ${_KTX_DIR}/lib/dfdutils/printdfd.c
        ${_KTX_DIR}/lib/dfdutils/queries.c
        ${_KTX_DIR}/lib/dfdutils/vk2dfd.c
)

# 包含路径（与 SCsub 对齐）
target_include_directories(godot PRIVATE
        ${_KTX_DIR}/include
        ${_KTX_DIR}/utils
        ${_KTX_DIR}/lib
        ${_KTX_DIR}/other_include
        ${CMAKE_SOURCE_DIR}/thirdparty/basis_universal
)

# Vulkan 相关：有 Vulkan 就加 include，否则定义 LIBKTX 走内置 vkformat_enum.h
if(USE_VULKAN)
    target_include_directories(godot PRIVATE
            ${CMAKE_SOURCE_DIR}/thirdparty/vulkan/include
    )
else()
    target_compile_definitions(godot PRIVATE LIBKTX)
endif()

# 编辑器构建时，禁用 miniz 源实现（与 SCsub 的 MINIZ_HEADER_FILE_ONLY 一致）
if(EDITOR_BUILD)
    target_compile_definitions(godot PRIVATE MINIZ_HEADER_FILE_ONLY)
endif()

# 固定的宏（与 SCsub 一致）
target_compile_definitions(godot PRIVATE KHRONOS_STATIC=1)

# 关闭第三方源码的告警（与 SCons 的 disable_warnings 对齐）
if(MSVC)
    set_source_files_properties(${_KTX_THIRDPARTY_SRCS} PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(${_KTX_THIRDPARTY_SRCS} PROPERTIES COMPILE_OPTIONS "-w")
endif()

# 将 thirdparty 源并入 godot 目标
target_sources(godot PRIVATE
        ${_KTX_THIRDPARTY_SRCS}
)

# 将本目录下的模块源码并入 godot（对应 env_ktx.add_source_files(module_obj, "*.cpp")）
file(GLOB _KTX_MODULE_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
target_sources(godot PRIVATE
        ${_KTX_MODULE_SRCS}
)

# 说明：
# - SCons 中通过 env.Depends(module_obj, thirdparty_obj) 强制当 thirdparty 变动时重编模块。
#   在 CMake/Ninja/MSVC 下，编译器会扫描头文件依赖，且 thirdparty 源也加入了目标，
#   因此不额外添加“依赖印记”文件也能正常触发重建。