# 子目录 CMakeLists：navigation 模块
# - 等价于 SCsub 逻辑：
#   * 可选内置 thirdparty：recastnavigation、rvo2_2d、rvo2_3d
#   * 模块自身源码：*.cpp、2d/*.cpp、(可选)3d/*.cpp、(可选)editor/*.cpp
#   * 第三方源码单独禁用告警
#   * 将以上源与包含目录并入上层已定义的目标 godot

# 确认上层已定义目标 godot（该 CMakeLists 作为子目录被添加）
if(NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Please add this subdirectory after defining the 'godot' target.")
endif()

# 允许上层通过缓存变量控制是否内置第三方与是否构建 3D/Editor 源
# 如上层已定义（-D...），则沿用；否则提供默认值
if(NOT DEFINED BUILTIN_RECASTNAVIGATION)
    option(BUILTIN_RECASTNAVIGATION "Build with builtin recastnavigation thirdparty" ON)
endif()
if(NOT DEFINED BUILTIN_RVO2_2D)
    option(BUILTIN_RVO2_2D "Build with builtin rvo2 2D thirdparty" ON)
endif()
if(NOT DEFINED BUILTIN_RVO2_3D)
    option(BUILTIN_RVO2_3D "Build with builtin rvo2 3D thirdparty" ON)
endif()
if(NOT DEFINED DISABLE_3D)
    option(DISABLE_3D "Disable 3D sources for this module" OFF)
endif()
if(NOT DEFINED EDITOR_BUILD)
    option(EDITOR_BUILD "Build editor sources for this module" OFF)
endif()

# -------- 第三方源码收集与设置（按需启用） --------

set(THIRDPARTY_SOURCES)
set(THIRDPARTY_INCLUDE_DIRS)

# Recast（等价于 env["builtin_recastnavigation"] 块）
if(BUILTIN_RECASTNAVIGATION)
    set(RECAST_DIR "${PROJECT_SOURCE_DIR}/thirdparty/recastnavigation/Recast")
    list(APPEND THIRDPARTY_INCLUDE_DIRS
            "${RECAST_DIR}/Include"
    )
    set(RECAST_SOURCES
            "${RECAST_DIR}/Source/Recast.cpp"
            "${RECAST_DIR}/Source/RecastAlloc.cpp"
            "${RECAST_DIR}/Source/RecastArea.cpp"
            "${RECAST_DIR}/Source/RecastAssert.cpp"
            "${RECAST_DIR}/Source/RecastContour.cpp"
            "${RECAST_DIR}/Source/RecastFilter.cpp"
            "${RECAST_DIR}/Source/RecastLayers.cpp"
            "${RECAST_DIR}/Source/RecastMesh.cpp"
            "${RECAST_DIR}/Source/RecastMeshDetail.cpp"
            "${RECAST_DIR}/Source/RecastRasterization.cpp"
            "${RECAST_DIR}/Source/RecastRegion.cpp"
    )
    list(APPEND THIRDPARTY_SOURCES ${RECAST_SOURCES})
endif()

# RVO 2D（等价于 env["builtin_rvo2_2d"] 块）
if(BUILTIN_RVO2_2D)
    set(RVO2_2D_DIR "${PROJECT_SOURCE_DIR}/thirdparty/rvo2/rvo2_2d")
    list(APPEND THIRDPARTY_INCLUDE_DIRS
            "${RVO2_2D_DIR}"
    )
    set(RVO2_2D_SOURCES
            "${RVO2_2D_DIR}/Agent2d.cpp"
            "${RVO2_2D_DIR}/Obstacle2d.cpp"
            "${RVO2_2D_DIR}/KdTree2d.cpp"
            "${RVO2_2D_DIR}/RVOSimulator2d.cpp"
    )
    list(APPEND THIRDPARTY_SOURCES ${RVO2_2D_SOURCES})
endif()

# RVO 3D（等价于 env["builtin_rvo2_3d"] 块）
if(BUILTIN_RVO2_3D)
    set(RVO2_3D_DIR "${PROJECT_SOURCE_DIR}/thirdparty/rvo2/rvo2_3d")
    list(APPEND THIRDPARTY_INCLUDE_DIRS
            "${RVO2_3D_DIR}"
    )
    set(RVO2_3D_SOURCES
            "${RVO2_3D_DIR}/Agent3d.cpp"
            "${RVO2_3D_DIR}/KdTree3d.cpp"
            "${RVO2_3D_DIR}/RVOSimulator3d.cpp"
    )
    list(APPEND THIRDPARTY_SOURCES ${RVO2_3D_SOURCES})
endif()

# 将第三方包含目录添加到 godot 目标（等价于 env_navigation.Prepend(CPPPATH=...)）
if(THIRDPARTY_INCLUDE_DIRS)
    target_include_directories(godot PRIVATE ${THIRDPARTY_INCLUDE_DIRS})
endif()

# 为第三方源码禁用告警（等价于 env_thirdparty.disable_warnings()）
# MSVC 使用 /w；GCC/Clang 使用 -w
if(THIRDPARTY_SOURCES)
    set_source_files_properties(${THIRDPARTY_SOURCES} PROPERTIES
            COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/w>$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>"
    )
endif()

# -------- 模块自身源码收集（等价于 add_source_files 模式） --------

set(MODULE_SOURCES)
# 基本与 2D
file(GLOB MODULE_SOURCES_BASE CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/2d/*.cpp"
)
list(APPEND MODULE_SOURCES ${MODULE_SOURCES_BASE})

# 可选 3D
if(NOT DISABLE_3D)
    file(GLOB MODULE_SOURCES_3D CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/3d/*.cpp"
    )
    list(APPEND MODULE_SOURCES ${MODULE_SOURCES_3D})
endif()

# 可选 editor
if(EDITOR_BUILD)
    file(GLOB MODULE_SOURCES_EDITOR CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp"
    )
    list(APPEND MODULE_SOURCES ${MODULE_SOURCES_EDITOR})
endif()

# -------- 将第三方与模块源码并入 godot 目标 --------

target_sources(godot PRIVATE
        ${MODULE_SOURCES}
        ${THIRDPARTY_SOURCES}
)

# 说明：
# - 在 SCons 中通过 env.Depends(module_obj, thirdparty_obj) 强制当第三方更新时重建模块。
#   在 CMake 中第三方源文件已直接加入 target_sources，任何 thirdparty 源变更都会触发重编译；
#   对第三方头文件更新，若被模块或第三方源包含，也会触发相应增量重建，无需额外依赖声明。