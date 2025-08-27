# CMake

# 选项开关：是否使用 volk、X11、Wayland（对应 SCons 的 env["use_volk"], env["x11"], env["wayland"]）
option(USE_VOLK "Use volk for Vulkan function loading" ON)
option(ENABLE_X11 "Enable X11 platform defines" ON)
option(ENABLE_WAYLAND "Enable Wayland platform defines" OFF)

# 路径
set(THIRDPARTY_VULKAN_DIR "${CMAKE_SOURCE_DIR}/thirdparty/vulkan")
set(THIRDPARTY_VOLK_DIR   "${CMAKE_SOURCE_DIR}/thirdparty/volk")

# 包含目录：始终使用自带 Vulkan 头
target_include_directories(godot PRIVATE
        "${THIRDPARTY_VULKAN_DIR}"
        "${THIRDPARTY_VULKAN_DIR}/include"
)

# 如果启用 volk，则加入 volk 头并定义 USE_VOLK
if(USE_VOLK)
    target_include_directories(godot PRIVATE "${THIRDPARTY_VOLK_DIR}")
    target_compile_definitions(godot PRIVATE USE_VOLK)
endif()

# 平台相关宏（尽量复刻 SCons 里的逻辑）
if(ANDROID)
    target_compile_definitions(godot PRIVATE VK_USE_PLATFORM_ANDROID_KHR)
elseif(APPLE)
    # 区分 iOS 与 macOS
    # 常见判定：CMAKE_SYSTEM_NAME=Darwin 且 CMAKE_OSX_SYSROOT 包含 iphoneos/iphonesimulator 时为 iOS
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR IOS)
        target_compile_definitions(godot PRIVATE VK_USE_PLATFORM_IOS_MVK VK_USE_PLATFORM_METAL_EXT)
    else()
        target_compile_definitions(godot PRIVATE VK_USE_PLATFORM_MACOS_MVK VK_USE_PLATFORM_METAL_EXT)
    endif()
elseif(WIN32)
    target_compile_definitions(godot PRIVATE VK_USE_PLATFORM_WIN32_KHR)
elseif(UNIX)
    # linuxbsd 情况：按开关设置 X11 / Wayland
    if(ENABLE_X11)
        target_compile_definitions(godot PRIVATE VK_USE_PLATFORM_XLIB_KHR)
    endif()
    if(ENABLE_WAYLAND)
        target_compile_definitions(godot PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
    endif()
endif()

# 第三方源码：VMA 与（可选）volk
set(VMA_SRC "${THIRDPARTY_VULKAN_DIR}/vk_mem_alloc.cpp")

# 编译第三方时尽量禁告（对应 SCons 的 disable_warnings）
# MSVC: /w ；GCC/Clang: -w
set_source_files_properties("${VMA_SRC}" PROPERTIES
        COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/w>;$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>"
)

# VMA 限制/宏
if(USE_VOLK)
    target_compile_definitions(godot PRIVATE VMA_STATIC_VULKAN_FUNCTIONS=1)
else()
    if(ANDROID)
        # 仅限旧 NDK：限制 VMA 到 1.0
        target_compile_definitions(godot PRIVATE VMA_VULKAN_VERSION=1000000)
    elseif(APPLE)
        # MoltenVK 仅支持 Vulkan 1.1
        target_compile_definitions(godot PRIVATE VMA_VULKAN_VERSION=1001000)
    endif()
endif()

# volk 源码（仅当启用 volk）
if(USE_VOLK)
    set(VOLK_SRC "${THIRDPARTY_VOLK_DIR}/volk.c")
    set_source_files_properties("${VOLK_SRC}" PROPERTIES
            COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/w>;$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>"
    )
endif()

# 本目录下的 Godot 源文件
file(GLOB LOCAL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 汇总要添加到目标的源码
set(DRIVER_SOURCES ${LOCAL_SOURCES} "${VMA_SRC}")
if(USE_VOLK)
    list(APPEND DRIVER_SOURCES "${VOLK_SRC}")
endif()

# 附加到已存在的可执行/库目标 godot
target_sources(godot PRIVATE ${DRIVER_SOURCES})