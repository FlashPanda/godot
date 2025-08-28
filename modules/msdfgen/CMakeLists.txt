# 最低 CMake 版本要求可按你工程的最小版本策略调整
cmake_minimum_required(VERSION 3.16)

# 该文件作为“子目录 CMakeLists”使用，一般不在此定义顶层 project()

# 选项：是否使用 thirdparty 目录中的内置 msdfgen 实现
# 可通过 -Dbuiltin_msdfgen=ON/OFF 在命令行/缓存中配置
option(builtin_msdfgen "Build and use the bundled thirdparty msdfgen library" ON)

# 方便引用工程根路径下的第三方目录（假定本子目录位于仓库根的子层级中）
# CMAKE_SOURCE_DIR 指向最外层 project() 定义所在的源代码根目录
set(THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/thirdparty")

# ---------------------------------------------------------------------------
# 1) 可选：构建内置 msdfgen 静态库（对应 SCons 中 builtin_msdfgen 分支逻辑）
# ---------------------------------------------------------------------------
if(builtin_msdfgen)
    # 第三方源码根
    set(MSDFGEN_THIRDPARTY_DIR "${THIRDPARTY_DIR}/msdfgen")

    # 与 SCsub 中一致的源文件列表（保持精确清单，避免引入不必要文件）
    set(MSDFGEN_THIRDPARTY_SOURCES
            "${MSDFGEN_THIRDPARTY_DIR}/core/Contour.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/EdgeHolder.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/MSDFErrorCorrection.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/Projection.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/Scanline.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/Shape.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/contour-combiners.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/edge-coloring.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/edge-segments.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/edge-selectors.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/equation-solver.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/msdf-error-correction.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/msdfgen.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/rasterization.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/render-sdf.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/sdf-error-estimation.cpp"
            "${MSDFGEN_THIRDPARTY_DIR}/core/shape-description.cpp"
    )

    # 生成静态库，名称沿用 SCons 中的 msdfgen_builtin
    add_library(msdfgen_builtin STATIC ${MSDFGEN_THIRDPARTY_SOURCES})

    # C++17 以匹配工程标准
    target_compile_features(msdfgen_builtin PUBLIC cxx_std_17)

    # 包含目录（与 SCsub 中 Prepend 的路径一致）
    target_include_directories(msdfgen_builtin
            PUBLIC
            "${THIRDPARTY_DIR}/freetype/include"
            "${THIRDPARTY_DIR}/msdfgen"
            "${THIRDPARTY_DIR}/nanosvg"
    )

    # 与 SCsub 中 Append 的 MSDFGEN_PUBLIC 定义等价：定义为空字符串
    target_compile_definitions(msdfgen_builtin
            PUBLIC
            MSDFGEN_PUBLIC=
    )

    # 可选：便于上层以别名链接（风格化）
    add_library(msdfgen::builtin ALIAS msdfgen_builtin)
endif()

# ---------------------------------------------------------------------------
# 2) 模块自身源码（对应 SCons 中 env_msdfgen.add_source_files(module_obj, "*.cpp")）
#    使用 OBJECT 库，便于上层聚合到最终目标；也能在第三方变更时触发重编译
# ---------------------------------------------------------------------------
# 搜索当前子目录下的所有 .cpp 模块源码
# 若不希望 GLOB，可手动维护清单；这里选择 GLOB 以贴近 SCons 的通配行为
file(GLOB MSDFGEN_MODULE_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 定义 OBJECT 库，名称示例：godot-module-msdfgen
add_library(godot-module-msdfgen OBJECT ${MSDFGEN_MODULE_SOURCES})

# C++17（与全工程一致）
target_compile_features(godot-module-msdfgen PUBLIC cxx_std_17)

# 如果启用内置 msdfgen，则让模块目标依赖并链接到该库
# 1) 依赖关系可确保第三方库更新时，模块对象能正确重建（CMake 的目标图自动处理）
# 2) 链接关系用于当上层将 OBJECT 库聚合到最终目标后，仍能保有对第三方库的链接需要
if(builtin_msdfgen)
    add_dependencies(godot-module-msdfgen msdfgen_builtin)

    # 注意：OBJECT 库不能直接“最终链接”，但可声明接口依赖，
    # 上层把 OBJECT 源并入最终目标时，可继承这些链接项。
    target_link_libraries(godot-module-msdfgen
            INTERFACE
            msdfgen_builtin
    )
endif()

# ---------------------------------------------------------------------------
# 3) 供上层使用的导出信息（可选）
#    - 将本模块的 OBJECT 库目标名导出到父作用域，便于主 CMake 聚合
#    - 如果你们有统一的全局属性或变量收集模块目标，也可以在这里 set_property(APPEND)
# ---------------------------------------------------------------------------
set(MSDFGEN_MODULE_OBJECT_TARGET godot-module-msdfgen PARENT_SCOPE)
if(builtin_msdfgen)
    set(MSDFGEN_BUILTIN_TARGET msdfgen_builtin PARENT_SCOPE)
endif()

# 说明：
# - 关于 SCons 中“将第三方库插入到特定链接顺序（例如在 libscene 之后、系统库之前）的 hack”，
#   在 CMake 中通常由上层目标在 target_link_libraries 调用中精确控制顺序来保证。
#   本子目录只声明目标与依赖；请在上层聚合/链接最终可执行或共享库时，按需安排 msdfgen_builtin 的位置。