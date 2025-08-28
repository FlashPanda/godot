# 子目录 CMakeLists.txt（与该模块源码同级放置）

# thirdparty/manifold 路径（位于工程根目录）
set(MANIFOLD_DIR "${PROJECT_SOURCE_DIR}/thirdparty/manifold")
set(MANIFOLD_INC "${MANIFOLD_DIR}/include")

# 显式列出 thirdparty 源文件（与 SCsub 列表一致）
set(MANIFOLD_THIRDPARTY_SOURCES
        "${MANIFOLD_DIR}/src/boolean_result.cpp"
        "${MANIFOLD_DIR}/src/boolean3.cpp"
        "${MANIFOLD_DIR}/src/constructors.cpp"
        "${MANIFOLD_DIR}/src/csg_tree.cpp"
        "${MANIFOLD_DIR}/src/edge_op.cpp"
        "${MANIFOLD_DIR}/src/face_op.cpp"
        "${MANIFOLD_DIR}/src/impl.cpp"
        "${MANIFOLD_DIR}/src/manifold.cpp"
        "${MANIFOLD_DIR}/src/polygon.cpp"
        "${MANIFOLD_DIR}/src/properties.cpp"
        "${MANIFOLD_DIR}/src/quickhull.cpp"
        "${MANIFOLD_DIR}/src/smoothing.cpp"
        "${MANIFOLD_DIR}/src/sort.cpp"
        "${MANIFOLD_DIR}/src/subdivision.cpp"
)

# 供上层控制是否包含 editor 源；若上层未设置，则提供默认 OFF
if(NOT DEFINED EDITOR_BUILD)
    option(EDITOR_BUILD "Build editor sources for this module" OFF)
endif()

# 模块自身源码
file(GLOB MODULE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
if(EDITOR_BUILD)
    file(GLOB MODULE_EDITOR_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp"
    )
    list(APPEND MODULE_SOURCES ${MODULE_EDITOR_SOURCES})
endif()

# thirdparty 对象库：禁用告警，使用 SYSTEM include，C++17，定义 MANIFOLD_PAR=-1
add_library(manifold_thirdparty OBJECT
        ${MANIFOLD_THIRDPARTY_SOURCES}
)
target_include_directories(manifold_thirdparty SYSTEM PRIVATE
        "${MANIFOLD_INC}"
)
target_compile_features(manifold_thirdparty PRIVATE cxx_std_17)
target_compile_definitions(manifold_thirdparty PRIVATE MANIFOLD_PAR=-1)
if(MSVC)
    target_compile_options(manifold_thirdparty PRIVATE /W0)
else()
    target_compile_options(manifold_thirdparty PRIVATE -w)
endif()

# 模块对象库：C++17，同样定义与包含路径
add_library(csg_module OBJECT
        ${MODULE_SOURCES}
)
target_compile_features(csg_module PRIVATE cxx_std_17)
target_compile_definitions(csg_module PRIVATE MANIFOLD_PAR=-1)
target_include_directories(csg_module PRIVATE
        "${MANIFOLD_INC}"
)

# 将对象并入上层可执行目标 godot
# 注：要求上层已定义 target(godot)
target_sources(godot PRIVATE
        $<TARGET_OBJECTS:manifold_thirdparty>
        $<TARGET_OBJECTS:csg_module>
)