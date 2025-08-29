# 子目录 CMakeLists.txt —— 等价实现 platform/linuxbsd/SCsub 的核心逻辑
# 使用方法：
# 1) 父级需先定义目标：add_executable(godot ...) 或 add_library(godot ...)
# 2) 父级中 add_subdirectory(本目录) 以启用
# 3) 通过 -D 选项控制条件源和调试符号拆分，见下方选项

cmake_minimum_required(VERSION 3.16)

# 0) 确认父级目标存在
if(NOT TARGET godot)
    message(FATAL_ERROR "未找到目标 'godot'。请在 add_subdirectory(本目录) 之前定义该目标。")
endif()

# 1) 选项映射（对标 SCsub 中的 env[...]）
option(LINUXBSD_USE_SOWRAP     "是否使用 *-so_wrap.c 软链接式加载"                OFF) # env["use_sowrap"]
option(LINUXBSD_X11            "是否启用 X11 支持（包含 x11 子目录源码）"         OFF) # env["x11"]
option(LINUXBSD_WAYLAND        "是否启用 Wayland 支持（包含 wayland 子目录源码）" OFF) # env["wayland"]
option(LINUXBSD_SPEECHD        "是否启用 Speech Dispatcher（加入 tts_linux.cpp）"  OFF) # env["speechd"]
option(LINUXBSD_FONTCONFIG     "是否启用 Fontconfig（可能加入 fontconfig-so_wrap.c）" OFF) # env["fontconfig"]
option(LINUXBSD_UDEV           "是否启用 libudev（可能加入 libudev-so_wrap.c）"      OFF) # env["udev"]
option(LINUXBSD_DBUS           "是否启用 D-Bus（可能加入 dbus-so_wrap.c）"           OFF) # env["dbus"]

# 调试拆分（近似 SCsub: env['debug_symbols'] && env['separate_debug_symbols']）
option(LINUXBSD_DEBUG_SYMBOLS          "是否编译带调试符号" OFF)               # env["debug_symbols"]
option(LINUXBSD_SEPARATE_DEBUG_SYMBOLS "是否将调试符号拆分为独立文件" OFF)     # env["separate_debug_symbols"]

# 2) 基础源文件（等价 SCsub: common_linuxbsd 列表）
set(_linuxbsd_sources
        crash_handler_linuxbsd.cpp
        os_linuxbsd.cpp
        joypad_linux.cpp
        freedesktop_portal_desktop.cpp
        freedesktop_screensaver.cpp
)

# 2.1) use_sowrap 时追加
if(LINUXBSD_USE_SOWRAP)
    list(APPEND _linuxbsd_sources xkbcommon-so_wrap.c)
endif()

# 2.2) x11/wayland 子目录的源码（SCsub 以 SConscript 汇总；此处用 GLOB 收集）
# 若你的子目录另外有各自 CMakeLists，可改为 add_subdirectory(xxx)
if(LINUXBSD_X11)
    file(GLOB _x11_sources CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/x11/*.[cC]"
            "${CMAKE_CURRENT_SOURCE_DIR}/x11/*.[cC][pP][pP]"
            "${CMAKE_CURRENT_SOURCE_DIR}/x11/*.[mM][mM]"
    )
    list(APPEND _linuxbsd_sources ${_x11_sources})
endif()

if(LINUXBSD_WAYLAND)
    file(GLOB _wayland_sources CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/wayland/*.[cC]"
            "${CMAKE_CURRENT_SOURCE_DIR}/wayland/*.[cC][pP][pP]"
            "${CMAKE_CURRENT_SOURCE_DIR}/wayland/*.[mM][mM]"
    )
    list(APPEND _linuxbsd_sources ${_wayland_sources})
endif()

# 2.3) speechd/fontconfig/udev/dbus 条件源
if(LINUXBSD_SPEECHD)
    list(APPEND _linuxbsd_sources tts_linux.cpp)
    if(LINUXBSD_USE_SOWRAP)
        list(APPEND _linuxbsd_sources speechd-so_wrap.c)
    endif()
endif()

if(LINUXBSD_FONTCONFIG AND LINUXBSD_USE_SOWRAP)
    list(APPEND _linuxbsd_sources fontconfig-so_wrap.c)
endif()

if(LINUXBSD_UDEV AND LINUXBSD_USE_SOWRAP)
    list(APPEND _linuxbsd_sources libudev-so_wrap.c)
endif()

if(LINUXBSD_DBUS AND LINUXBSD_USE_SOWRAP)
    list(APPEND _linuxbsd_sources dbus-so_wrap.c)
endif()

# 3) 将源文件并入父级目标 godot（等价 SCsub: env.add_program("#bin/godot", ["godot_linuxbsd.cpp"] + common_linuxbsd)）
# 在 CMake 中，我们把该平台文件也追加进去
list(INSERT _linuxbsd_sources 0 godot_linuxbsd.cpp)
target_sources(godot PRIVATE ${_linuxbsd_sources})

# 4) 可选：分离调试符号（等价 SCsub 的 AddPostAction(make_debug_linuxbsd) 近似行为）
# 说明：
# - 使用 objcopy/strip 提取并分离调试信息，随后将 .debug 以 gnu-debuglink 方式关联回可执行文件
# - 仅在 Unix 且非 Apple 的环境默认启用（更符合 Linux/BSD）
if(UNIX AND NOT APPLE AND LINUXBSD_DEBUG_SYMBOLS AND LINUXBSD_SEPARATE_DEBUG_SYMBOLS)
    # 选择工具：优先使用 CMake 已知工具，否则回退系统命令名
    if(DEFINED CMAKE_OBJCOPY)
        set(_OBJCOPY "${CMAKE_OBJCOPY}")
    else()
        set(_OBJCOPY objcopy)
    endif()

    if(DEFINED CMAKE_STRIP)
        set(_STRIP "${CMAKE_STRIP}")
    else()
        set(_STRIP strip)
    endif()

    # 仅当 'godot' 是可执行目标时执行（若是库请按需调整命令）
    get_target_property(_godot_type godot TYPE)
    if(_godot_type STREQUAL "EXECUTABLE")
        # 生成 <godot>.debug 文件，剥离并回填 gnu debuglink
        add_custom_command(TARGET godot POST_BUILD
                BYPRODUCTS "$<TARGET_FILE:godot>.debug"
                COMMAND "${_OBJCOPY}" --only-keep-debug "$<TARGET_FILE:godot>" "$<TARGET_FILE:godot>.debug"
                COMMAND "${_STRIP}" --strip-debug --strip-unneeded "$<TARGET_FILE:godot>"
                COMMAND "${_OBJCOPY}" --add-gnu-debuglink="$<TARGET_FILE:godot>.debug" "$<TARGET_FILE:godot>"
                COMMENT "Splitting debug symbols for target 'godot' (creating $<TARGET_FILE:godot>.debug)"
                VERBATIM
        )
    else()
        message(WARNING "目标 'godot' 不是可执行文件，跳过分离调试符号步骤。")
    endif()
endif()