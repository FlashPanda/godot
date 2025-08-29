# 子目录 CMakeLists.txt —— 等价实现 SCsub（openxr 模块）

# 该子目录应在上层定义/创建好目标 godot 之后再被 add_subdirectory 引入
if(NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Please include this subdirectory after defining 'godot'.")
endif()

# 可由上层或用户配置的渲染后端开关，对应 SCons 中的 env['vulkan'] / env['metal'] / env['opengl3']
# 默认均为 OFF，按需在上层开启
option(GODOT_OPENXR_VULKAN   "Enable OpenXR Vulkan extension source"   OFF)
option(GODOT_OPENXR_METAL    "Enable OpenXR Metal extension source"    OFF)
option(GODOT_OPENXR_OPENGL3  "Enable OpenXR OpenGL 3 extension source" OFF)

# 1) 收集当前目录下的 *.cpp（等价于 env_openxr.add_source_files(module_obj, \"*.cpp\")）
file(GLOB OPENXR_MODULE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 2) 平台/后端相关的附加源码（和 SCsub 保持一致）
# - Android: platform/openxr_android_extension.cpp
if(ANDROID)
    list(APPEND OPENXR_MODULE_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/openxr_android_extension.cpp"
    )
endif()

# - Vulkan: platform/openxr_vulkan_extension.cpp
if(GODOT_OPENXR_VULKAN)
    list(APPEND OPENXR_MODULE_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/openxr_vulkan_extension.cpp"
    )
endif()

# - Metal: platform/openxr_metal_extension.mm
#   仅在 Apple 系统上才有意义；.mm 为 Objective‑C++ 源，CMake 会按扩展名自动处理
if(GODOT_OPENXR_METAL AND APPLE)
    list(APPEND OPENXR_MODULE_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/openxr_metal_extension.mm"
    )
endif()

# - OpenGL 3: platform/openxr_opengl_extension.cpp，且不在 macOS 上启用
#   SCons 判断为 env['platform'] != 'macos'；这里用 (NOT (APPLE AND NOT IOS)) 近似“非 macOS”
if(GODOT_OPENXR_OPENGL3 AND NOT (APPLE AND NOT IOS))
    list(APPEND OPENXR_MODULE_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/openxr_opengl_extension.cpp"
    )
endif()

# 3) 将汇总后的模块源并入上层的 'godot' 目标（等价于 env.modules_sources += module_obj）
if(OPENXR_MODULE_SOURCES)
    target_sources(godot PRIVATE ${OPENXR_MODULE_SOURCES})
endif()