# CMakeLists.txt —— 等价实现 raycast 模块 SCsub（含内置 Embree 第三方）
cmake_minimum_required(VERSION 3.16)

# 由上层或用户配置：是否内置构建 Embree
option(GODOT_BUILTIN_EMBREE "Build bundled Embree from thirdparty" ON)

# 依赖：上层应已定义目标 godot
if(NOT TARGET godot)
    message(FATAL_ERROR "未找到目标 'godot'。请在定义/创建 'godot' 目标之后再 add_subdirectory 当前目录。")
endif()

# ========== 1) 收集并加入本模块源码（等价于 env_raycast.add_source_files(module_obj, \"*.cpp\")） ==========
file(GLOB CONFIGURE_DEPENDS RAYCAST_MODULE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(RAYCAST_MODULE_SOURCES)
    # 与 SCsub 中的 env_raycast（对 MSVC 额外禁告）保持一致
    if(MSVC)
        # 禁用“关于有意结构体填充的虚假告警”/wd4324
        target_compile_options(godot PRIVATE /wd4324)
    endif()

    target_sources(godot PRIVATE ${RAYCAST_MODULE_SOURCES})
endif()

# ========== 2) 内置 Embree（第三方）等价逻辑 ==========
if(GODOT_BUILTIN_EMBREE)
    # 源根目录（与 SCsub 内路径一致）
    set(EMBREE_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/embree")

    # Embree 源清单（按 SCsub）
    set(EMBREE_SRC_REL
            common/sys/sysinfo.cpp
            common/sys/alloc.cpp
            common/sys/estring.cpp
            common/sys/filename.cpp
            common/sys/library.cpp
            common/sys/thread.cpp
            common/sys/regression.cpp
            common/sys/mutex.cpp
            common/sys/condition.cpp
            common/sys/barrier.cpp
            common/math/constants.cpp
            common/simd/sse.cpp
            common/lexers/stringstream.cpp
            common/lexers/tokenstream.cpp
            common/tasking/taskschedulerinternal.cpp
            kernels/common/device.cpp
            kernels/common/stat.cpp
            kernels/common/acceln.cpp
            kernels/common/accelset.cpp
            kernels/common/state.cpp
            kernels/common/rtcore.cpp
            kernels/common/rtcore_builder.cpp
            kernels/common/scene.cpp
            kernels/common/scene_verify.cpp
            kernels/common/alloc.cpp
            kernels/common/geometry.cpp
            kernels/common/scene_triangle_mesh.cpp
            kernels/geometry/primitive4.cpp
            kernels/builders/primrefgen.cpp
            kernels/bvh/bvh.cpp
            kernels/bvh/bvh_statistics.cpp
            kernels/bvh/bvh4_factory.cpp
            kernels/bvh/bvh8_factory.cpp
            kernels/bvh/bvh_collider.cpp
            kernels/bvh/bvh_rotate.cpp
            kernels/bvh/bvh_refit.cpp
            kernels/bvh/bvh_builder.cpp
            kernels/bvh/bvh_builder_morton.cpp
            kernels/bvh/bvh_builder_sah.cpp
            kernels/bvh/bvh_builder_sah_spatial.cpp
            kernels/bvh/bvh_builder_sah_mb.cpp
            kernels/bvh/bvh_builder_twolevel.cpp
            kernels/bvh/bvh_intersector1_bvh4.cpp
            kernels/bvh/bvh_intersector_hybrid4_bvh4.cpp
    )
    set(EMBREE_SOURCES "")
    foreach(_rel ${EMBREE_SRC_REL})
        list(APPEND EMBREE_SOURCES "${EMBREE_ROOT}/${_rel}")
    endforeach()

    # 使用 OBJECT 库以便把对象文件直接并入 godot（贴近 SCons 行为）
    add_library(embree_thirdparty_obj OBJECT ${EMBREE_SOURCES})

    # 第三方包含目录（SCsub: Prepend CPPPATH=[thirdparty_dir, thirdparty_dir/include]）
    target_include_directories(embree_thirdparty_obj
            PUBLIC
            "${EMBREE_ROOT}"
            "${EMBREE_ROOT}/include"
    )
    # 也给 godot 模块可见，便于本目录源码包含 embree 头
    target_include_directories(godot PRIVATE
            "${EMBREE_ROOT}"
            "${EMBREE_ROOT}/include"
    )

    # 预处理宏（SCsub：EMBREE_TARGET_SSE2, EMBREE_LOWEST_ISA, TASKING_INTERNAL, 且强制 NDEBUG）
    target_compile_definitions(embree_thirdparty_obj
            PRIVATE
            EMBREE_TARGET_SSE2
            EMBREE_LOWEST_ISA
            TASKING_INTERNAL
            NDEBUG
    )

    # 平台相关（Windows：psapi，且非 MSVC 要 -mstackrealign）
    if(WIN32)
        if(MSVC)
            # MSVC 下仅链接 psapi；禁告 /wd4324 已在上面对 godot 设置
            target_link_libraries(godot PRIVATE psapi)
        else()
            # MinGW/Clang-cl（非 MSVC）：同样链接 psapi，并对 embree 源设置栈实对齐
            target_link_libraries(godot PRIVATE psapi)
            target_compile_options(embree_thirdparty_obj PRIVATE -mstackrealign)
        endif()
    endif()

    # 若架构非 x86_64 或使用 MSVC，则定义 __SSE2__ / __SSE__（SCsub 等价逻辑）
    # 说明：MSVC 的 SIMD 处理与宏定义相关性不同，这里直接与 SCsub 对齐。
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" OR MSVC)
        target_compile_definitions(embree_thirdparty_obj PRIVATE __SSE2__ __SSE__)
    endif()

    # Web 平台：开启 -msimd128（对应 SCsub 的 env_thirdparty.Append(CXXFLAGS=[-msimd128])）
    if(EMSCRIPTEN)
        target_compile_options(embree_thirdparty_obj PRIVATE -msimd128)
    endif()

    # 非 MSVC：与上游 gnu.cmake 同步的编译选项
    if(NOT MSVC)
        # aarch64/Linux/GNU 编译器：-flax-vector-conversions
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64" AND UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(embree_thirdparty_obj PRIVATE -flax-vector-conversions)
        endif()

        target_compile_options(embree_thirdparty_obj PRIVATE
                -fno-strict-overflow
                -fno-delete-null-pointer-checks
                -fwrapv
                -fsigned-char
                -fno-strict-aliasing
                -fno-tree-vectorize
                -fvisibility=hidden
                -fvisibility-inlines-hidden
        )
    endif()

    # 第三方禁告（SCsub: env_thirdparty.disable_warnings()）
    if(MSVC)
        target_compile_options(embree_thirdparty_obj PRIVATE /W0)
    else()
        target_compile_options(embree_thirdparty_obj PRIVATE -w)
    endif()

    # 将第三方对象并入 godot（等价 env.modules_sources += thirdparty_obj）
    target_sources(godot PRIVATE $<TARGET_OBJECTS:embree_thirdparty_obj>)
endif()

# ========== 3) 说明 ==========
# - 若需强制在第三方更新时重建本模块，CMake 会根据源文件时间戳正确处理。
# - 如果需要更细粒度的“依赖触发”，可考虑将第三方生成产物作为 BYPRODUCTS 的自定义命令/目标。