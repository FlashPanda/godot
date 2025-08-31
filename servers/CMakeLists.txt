# CMake

# 本文件为“子目录”构建脚本，不在此处设置 cmake_minimum_required()/project()。
# 目的：复刻同目录 SCons 构建逻辑，组织 servers 相关源码并链接到上层主目标。

# 可配置项：
# 1) 上层主目标名称（应在更高层 CMakeLists.txt 中已创建：add_executable()/add_library()）
set(GODOT_TARGET "godot" CACHE STRING "上层已创建的构建目标名称")

# 2) 是否禁用 3D（对应 SCons 的 env[\"disable_3d\"]）
option(DISABLE_3D "禁用 3D 相关源码与 xr 子目录" OFF)

# 基本校验：要求上层已存在主目标
if(NOT TARGET ${GODOT_TARGET})
    message(FATAL_ERROR "未找到构建目标 ${GODOT_TARGET}。请在上层先创建该目标，或通过 -DGODOT_TARGET=... 指定。")
endif()

# -----------------------------------------------------------------------------
# 收集当前目录中的源文件（对应 SCsub 中的 add_source_files 列表）
# -----------------------------------------------------------------------------
set(servers_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/audio_server.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/camera_server.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/display_server.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/navigation_server_2d.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/navigation_server_3d.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/physics_server_2d.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/physics_server_2d_wrap_mt.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/register_server_types.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/rendering_server.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/text_server.cpp"
)

# 根据 DISABLE_3D 决定是否加入 3D 相关源码
if(NOT DISABLE_3D)
    list(APPEND servers_sources
            "${CMAKE_CURRENT_SOURCE_DIR}/physics_server_3d.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/physics_server_3d_wrap_mt.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/xr_server.cpp"
    )
endif()

# -----------------------------------------------------------------------------
# 递归进入子目录（等价于一系列 SConscript 调用）
# 仅当子目录存在且其中有 CMakeLists.txt 时才 add_subdirectory，避免空目录报错。
# -----------------------------------------------------------------------------
set(_subdirs
        audio
        camera
        debugger
        display
        extensions
        movie_writer
        navigation
        rendering
        text
)

foreach(_d IN LISTS _subdirs)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_d}/CMakeLists.txt")
        add_subdirectory("${_d}")
    endif()
endforeach()

# 3D 开启时尝试进入 xr 子目录
if(NOT DISABLE_3D)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/xr/CMakeLists.txt")
        add_subdirectory(xr)
    endif()
endif()

# -----------------------------------------------------------------------------
# 生成静态库 servers（对应 SCons 的 env.add_library("servers", ...))
# 并把该库链接到上层主目标（对应 SCons 的 env.Prepend(LIBS=[lib])）
# -----------------------------------------------------------------------------
add_library(servers STATIC ${servers_sources})

# 可根据需要添加该库的公共/私有包含目录、编译定义、编译选项，示例：
# target_include_directories(servers PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# target_compile_features(servers PUBLIC cxx_std_17)

# 链接到上层主目标
target_link_libraries(${GODOT_TARGET} PRIVATE servers)