# CMakeLists.txt (子目录)
# 说明：
# - 该文件用于替代同目录下原有的 SCsub 功能。
# - 通过 OBJECT 库导出对象文件，方便父工程统一聚合进最终目标（与 Godot 的模块聚合模式类似）。
# - 第三方 meshoptimizer 源码会关闭编译告警，避免污染工程告警输出。
# - 当 thirdparty 源码更新时，本模块也会被触发重建（通过目标依赖与对象聚合实现）。

cmake_minimum_required(VERSION 3.20)

# 约定：
# - 父工程已通过 project(...) 定义工程。
# - 本 CMakeLists 作为子目录被 add_subdirectory(...) 引入。

# ------------------------------------------------------------------------------
# 1) 定义 thirdparty/meshoptimizer 源码清单
#    注意：thirdparty 位于仓库根目录，使用 PROJECT_SOURCE_DIR 进行定位。
# ------------------------------------------------------------------------------

set(MESHOPTIMIZER_3P_DIR "${PROJECT_SOURCE_DIR}/thirdparty/meshoptimizer")

set(MESHOPTIMIZER_3P_SOURCES
        "${MESHOPTIMIZER_3P_DIR}/allocator.cpp"
        "${MESHOPTIMIZER_3P_DIR}/clusterizer.cpp"
        "${MESHOPTIMIZER_3P_DIR}/indexcodec.cpp"
        "${MESHOPTIMIZER_3P_DIR}/indexgenerator.cpp"
        "${MESHOPTIMIZER_3P_DIR}/overdrawanalyzer.cpp"
        "${MESHOPTIMIZER_3P_DIR}/overdrawoptimizer.cpp"
        "${MESHOPTIMIZER_3P_DIR}/simplifier.cpp"
        "${MESHOPTIMIZER_3P_DIR}/spatialorder.cpp"
        "${MESHOPTIMIZER_3P_DIR}/stripifier.cpp"
        "${MESHOPTIMIZER_3P_DIR}/vcacheanalyzer.cpp"
        "${MESHOPTIMIZER_3P_DIR}/vcacheoptimizer.cpp"
        "${MESHOPTIMIZER_3P_DIR}/vertexcodec.cpp"
        "${MESHOPTIMIZER_3P_DIR}/vertexfilter.cpp"
        "${MESHOPTIMIZER_3P_DIR}/vfetchanalyzer.cpp"
        "${MESHOPTIMIZER_3P_DIR}/vfetchoptimizer.cpp"
)

# ------------------------------------------------------------------------------
# 2) 构建第三方 OBJECT 库，并关闭告警
# ------------------------------------------------------------------------------

add_library(meshoptimizer_thirdparty_obj OBJECT ${MESHOPTIMIZER_3P_SOURCES})

# 第三方头文件目录（若源码包含自有头文件）
target_include_directories(meshoptimizer_thirdparty_obj
        PUBLIC
        "${MESHOPTIMIZER_3P_DIR}"
)

# 关闭第三方告警（等价于 SCons 中的 disable_warnings）
if(MSVC)
    target_compile_options(meshoptimizer_thirdparty_obj PRIVATE /W0)  # 关闭 MSVC 告警
else()
    target_compile_options(meshoptimizer_thirdparty_obj PRIVATE -w)   # 关闭 GCC/Clang 告警
endif()

# ------------------------------------------------------------------------------
# 3) 收集本模块源码（等价于 add_source_files(module_obj, "*.cpp")）
#    使用 GLOB 配合 CONFIGURE_DEPENDS，便于自动跟踪新增/删除的 .cpp 文件。
# ------------------------------------------------------------------------------

file(GLOB MODULE_MESHOPTIMIZER_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# ------------------------------------------------------------------------------
# 4) 构建本模块的 OBJECT 库
#    - 这里不直接把 thirdparty 源码并入该目标的源文件，而是保持独立的 OBJECT 库，
#      以贴近原 SCons “分别收集再聚合”的行为。
#    - 通过 add_dependencies 确保 thirdparty 改动会触发本模块的重建（源于对象级依赖关系）。
# ------------------------------------------------------------------------------

add_library(module_meshoptimizer_obj OBJECT ${MODULE_MESHOPTIMIZER_SOURCES})

target_include_directories(module_meshoptimizer_obj
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${MESHOPTIMIZER_3P_DIR}"   # 若本模块源码直接包含 thirdparty 头文件
)

# 保证构建顺序与依赖（thirdparty 更新时，模块也会联动重建）
add_dependencies(module_meshoptimizer_obj meshoptimizer_thirdparty_obj)

# ------------------------------------------------------------------------------
# 5) 可选：导出对象文件给父级聚合
#    - 如果父级定义了一个用于收集模块对象文件的变量（示例：GODOT_MODULE_OBJECTS），
#      这里将本模块与 thirdparty 的对象一并追加并传回父作用域。
#    - 父级随后可将该变量整体链接进最终的静态/可执行目标。
# ------------------------------------------------------------------------------

# 约定变量名（如父级未定义，也可在此处无害地创建空变量再回传）
set(GODOT_MODULE_OBJECTS ${GODOT_MODULE_OBJECTS}
        $<TARGET_OBJECTS:module_meshoptimizer_obj>
        $<TARGET_OBJECTS:meshoptimizer_thirdparty_obj>
        PARENT_SCOPE
)

# ------------------------------------------------------------------------------
# 6) 可选：也可以在本子目录内直接生成一个静态库，供父级直接 target_link_libraries(...)
#    - 如需这种用法，取消下面注释：
# ------------------------------------------------------------------------------

# add_library(godot_module_meshoptimizer STATIC
#     $<TARGET_OBJECTS:meshoptimizer_thirdparty_obj>
#     $<TARGET_OBJECTS:module_meshoptimizer_obj>
# )
# target_include_directories(godot_module_meshoptimizer
#     PUBLIC
#         "${CMAKE_CURRENT_SOURCE_DIR}"
#         "${MESHOPTIMIZER_3P_DIR}"
# )