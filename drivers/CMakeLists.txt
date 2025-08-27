# CMakeLists.txt（子目录最小实现）

# 聚合子目录追加的源码的全局属性（若子目录不追加也无妨）
define_property(GLOBAL PROPERTY GODOT_DRIVERS_SOURCES
        BRIEF_DOCS "Aggregated driver sources from driver subdirectories"
        FULL_DOCS  "All driver sources appended by subdirectories to build the 'drivers' target"
)

# 读取上层传入的控制变量（不在此处设置默认值，保持子目录纯粹）
set(GODOT_PLATFORM           "${GODOT_PLATFORM}")
set(GODOT_DRIVERS_SUPPORTED  "${GODOT_DRIVERS_SUPPORTED}")
set(GODOT_VULKAN             "${GODOT_VULKAN}")
set(GODOT_D3D12              "${GODOT_D3D12}")
set(GODOT_OPENGL3            "${GODOT_OPENGL3}")
set(GODOT_METAL              "${GODOT_METAL}")
set(GODOT_XAUDIO2            "${GODOT_XAUDIO2}")

# 简易检查函数：若启用了某特性但不在支持列表，直接报错（模仿 SCsub 的行为）
function(_require_supported tag pretty_name)
    if(NOT "${GODOT_DRIVERS_SUPPORTED}" STREQUAL "")
        # 将支持列表转为 CMake 列表后查询
        set(_supported_list "${GODOT_DRIVERS_SUPPORTED}")
        list(FIND _supported_list "${tag}" _idx)
        if(_idx EQUAL -1)
            message(FATAL_ERROR
                    "Target platform '${GODOT_PLATFORM}' does not support the ${pretty_name} driver (missing '${tag}' in GODOT_DRIVERS_SUPPORTED).")
        endif()
    endif()
endfunction()

# 进入各 OS 驱动子目录（存在才进入，避免强制依赖）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unix/CMakeLists.txt")
    add_subdirectory(unix)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/windows/CMakeLists.txt")
    add_subdirectory(windows)
endif()

# 声音驱动
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/alsa/CMakeLists.txt")
    add_subdirectory(alsa)
endif()
if("${GODOT_PLATFORM}" STREQUAL "ios" OR "${GODOT_PLATFORM}" STREQUAL "macos")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/coreaudio/CMakeLists.txt")
        add_subdirectory(coreaudio)
    endif()
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pulseaudio/CMakeLists.txt")
    add_subdirectory(pulseaudio)
endif()
if("${GODOT_PLATFORM}" STREQUAL "windows")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/wasapi/CMakeLists.txt")
        add_subdirectory(wasapi)
    endif()
    if(NOT MSVC AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/backtrace/CMakeLists.txt")
        add_subdirectory(backtrace)
    endif()
endif()

if(GODOT_XAUDIO2)
    _require_supported("xaudio2" "XAudio2 audio")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/xaudio2/CMakeLists.txt")
        add_subdirectory(xaudio2)
    else()
        message(FATAL_ERROR "GODOT_XAUDIO2=ON but 'xaudio2' subdirectory is missing.")
    endif()
endif()

# MIDI 驱动
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/alsamidi/CMakeLists.txt")
    add_subdirectory(alsamidi)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/coremidi/CMakeLists.txt")
    add_subdirectory(coremidi)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/winmidi/CMakeLists.txt")
    add_subdirectory(winmidi)
endif()

# 图形驱动
if(GODOT_VULKAN)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/CMakeLists.txt")
        add_subdirectory(vulkan)
    endif()
endif()

if(GODOT_D3D12)
    _require_supported("d3d12" "D3D12 rendering")
    if(NOT "${GODOT_PLATFORM}" STREQUAL "windows")
        message(FATAL_ERROR "D3D12 is only available on Windows.")
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/d3d12/CMakeLists.txt")
        add_subdirectory(d3d12)
    else()
        message(FATAL_ERROR "GODOT_D3D12=ON but 'd3d12' subdirectory is missing.")
    endif()
endif()

if(GODOT_OPENGL3)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gl_context/CMakeLists.txt")
        add_subdirectory(gl_context)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gles3/CMakeLists.txt")
        add_subdirectory(gles3)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/egl/CMakeLists.txt")
        add_subdirectory(egl)
    endif()
endif()

if(GODOT_METAL)
    _require_supported("metal" "Metal rendering")
    if(NOT ("${GODOT_PLATFORM}" STREQUAL "macos" OR "${GODOT_PLATFORM}" STREQUAL "ios"))
        message(FATAL_ERROR "Metal is only available on Apple platforms (macOS/iOS).")
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/metal/CMakeLists.txt")
        add_subdirectory(metal)
    else()
        message(FATAL_ERROR "GODOT_METAL=ON but 'metal' subdirectory is missing.")
    endif()
endif()

# 核心依赖
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/png/CMakeLists.txt")
    add_subdirectory(png)
endif()

# 收集本目录的 *.cpp
file(GLOB _drivers_local_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 收集子目录追加的源
get_property(_drivers_sub_sources GLOBAL PROPERTY GODOT_DRIVERS_SOURCES)
if(NOT _drivers_sub_sources)
    set(_drivers_sub_sources "")
endif()

# 生成静态库：drivers
add_library(drivers STATIC
        ${_drivers_local_sources}
        ${_drivers_sub_sources}
)

target_include_directories(drivers PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 供父目录使用：直接 link 该目标即可
# 上层示例：
#   add_subdirectory(path/to/this)
#   target_link_libraries(<parent_target> PRIVATE drivers)