# 子目录 CMakeLists.txt —— 等价复现 SCsub 的功能

# 确保上层已定义可执行目标 godot
if(NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Please add this subdirectory after defining the 'godot' target.")
endif()

# 选项：是否内置编译 thirdparty/enet
if(NOT DEFINED builtin_enet)
    set(builtin_enet ON CACHE BOOL "Build with built-in thirdparty/enet")
endif()

# 1) 收集当前目录下的 *.cpp，并附加到 godot
file(GLOB MODULE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
if(MODULE_SOURCES)
    target_sources(godot PRIVATE ${MODULE_SOURCES})
endif()

# 2) 当启用 builtin_enet 时，附加 thirdparty/enet 源、包含目录、宏定义
if(builtin_enet)
    set(THIRDPARTY_ENET_DIR "${PROJECT_SOURCE_DIR}/thirdparty/enet")

    # 与 SCsub 中的文件列表保持一致
    set(THIRDPARTY_ENET_SOURCES
            "${THIRDPARTY_ENET_DIR}/godot.cpp"
            "${THIRDPARTY_ENET_DIR}/callbacks.c"
            "${THIRDPARTY_ENET_DIR}/compress.c"
            "${THIRDPARTY_ENET_DIR}/host.c"
            "${THIRDPARTY_ENET_DIR}/list.c"
            "${THIRDPARTY_ENET_DIR}/packet.c"
            "${THIRDPARTY_ENET_DIR}/peer.c"
            "${THIRDPARTY_ENET_DIR}/protocol.c"
    )

    # 将第三方源码直接并入 godot；这会在相关文件变化时自动触发重编译
    target_sources(godot PRIVATE ${THIRDPARTY_ENET_SOURCES})

    # 等价于 env_enet.Prepend(CPPPATH=[thirdparty_dir])
    target_include_directories(godot PRIVATE "${THIRDPARTY_ENET_DIR}")

    # 等价于 env_enet.Append(CPPDEFINES=["GODOT_ENET"])
    target_compile_definitions(godot PRIVATE GODOT_ENET)
endif()