# cmake

# -----------------------------------------------------------------------------
# 0) 选择父级目标名称
#    - 默认父级目标名为 godot（可执行/库）。
#    - 如父级使用不同目标名，可在 add_subdirectory 之前设置 GODOT_PARENT_TARGET 覆盖。
if (NOT DEFINED GODOT_PARENT_TARGET)
    set(GODOT_PARENT_TARGET godot)
endif()

if (NOT TARGET ${GODOT_PARENT_TARGET})
    message(FATAL_ERROR
            "未找到父级目标 '${GODOT_PARENT_TARGET}'。请先在父级 CMakeLists.txt 中定义该目标，"
            "再 add_subdirectory 进入本目录，或通过 set(GODOT_PARENT_TARGET <目标名>) 指定。")
endif()

# -----------------------------------------------------------------------------
# 1) 第三方 VHACD 源与包含目录（等价 SCons: thirdparty/vhacd + CPPPATH/inc）
#    说明：
#    - 这里使用 PROJECT_SOURCE_DIR 作为工程根，兼容从任意子目录调用。
set(VHACD_THIRDPARTY_DIR "${PROJECT_SOURCE_DIR}/thirdparty/vhacd")

set(VHACD_THIRDPARTY_SOURCES
        "${VHACD_THIRDPARTY_DIR}/src/vhacdManifoldMesh.cpp"
        "${VHACD_THIRDPARTY_DIR}/src/FloatMath.cpp"
        "${VHACD_THIRDPARTY_DIR}/src/vhacdMesh.cpp"
        "${VHACD_THIRDPARTY_DIR}/src/vhacdICHull.cpp"
        "${VHACD_THIRDPARTY_DIR}/src/vhacdVolume.cpp"
        "${VHACD_THIRDPARTY_DIR}/src/VHACD-ASYNC.cpp"
        "${VHACD_THIRDPARTY_DIR}/src/btAlignedAllocator.cpp"
        "${VHACD_THIRDPARTY_DIR}/src/vhacdRaycastMesh.cpp"
        "${VHACD_THIRDPARTY_DIR}/src/VHACD.cpp"
        "${VHACD_THIRDPARTY_DIR}/src/btConvexHullComputer.cpp"
)

# 添加包含目录（相当于 env_vhacd.Prepend(CPPPATH=[thirdparty_dir + "inc"]））
target_include_directories(${GODOT_PARENT_TARGET} PRIVATE
        "${VHACD_THIRDPARTY_DIR}/inc"
)

# 将 thirdparty 源并入父级构建目标（相当于 env.modules_sources += thirdparty_obj）
target_sources(${GODOT_PARENT_TARGET} PRIVATE ${VHACD_THIRDPARTY_SOURCES})

# 尽量为 thirdparty 源关闭编译告警（模拟 env_thirdparty.disable_warnings()）
if (MSVC)
    set_source_files_properties(${VHACD_THIRDPARTY_SOURCES}
            PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(${VHACD_THIRDPARTY_SOURCES}
            PROPERTIES COMPILE_OPTIONS "-w")
endif()

# -----------------------------------------------------------------------------
# 2) 模块自身源码（等价 SCons: env_vhacd.add_source_files(module_obj, "*.cpp")）
#    说明：
#    - 直接把本目录下的 *.cpp 并入父级目标；
#    - 将 thirdparty 与模块源同属一个目标，天然满足“thirdparty 更新时强制重编模块”的需求，
#      等价于 SCons 中的 env.Depends(module_obj, thirdparty_obj)。
file(GLOB MODULE_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

if (MODULE_LOCAL_SOURCES)
    target_sources(${GODOT_PARENT_TARGET} PRIVATE ${MODULE_LOCAL_SOURCES})
endif()
