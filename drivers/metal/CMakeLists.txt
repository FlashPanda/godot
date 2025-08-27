# 仅在 Apple 平台构建 Metal 驱动
if(NOT APPLE)
    return()
endif()

# 第三方 SPIRV-Cross 源
set(THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/thirdparty/spirv-cross")
set(THIRDPARTY_SOURCES
        "${THIRDPARTY_DIR}/spirv_cfg.cpp"
        "${THIRDPARTY_DIR}/spirv_cross_util.cpp"
        "${THIRDPARTY_DIR}/spirv_cross.cpp"
        "${THIRDPARTY_DIR}/spirv_parser.cpp"
        "${THIRDPARTY_DIR}/spirv_msl.cpp"
        "${THIRDPARTY_DIR}/spirv_reflect.cpp"
        "${THIRDPARTY_DIR}/spirv_glsl.cpp"
        "${THIRDPARTY_DIR}/spirv_cross_parsed_ir.cpp"
)

# 将 SPIRV-Cross 编译为 OBJECT 库，便于：
# - 单独开启异常（-fexceptions / /EHsc）
# - 禁用其编译警告
# - 配置仅对其生效的包含路径
add_library(spirv_cross_obj OBJECT ${THIRDPARTY_SOURCES})
target_include_directories(spirv_cross_obj PRIVATE
        "${THIRDPARTY_DIR}"
        "${THIRDPARTY_DIR}/include"
)
if(MSVC)
    target_compile_options(spirv_cross_obj PRIVATE /EHsc /w)
else()
    target_compile_options(spirv_cross_obj PRIVATE -fexceptions -w)
endif()

# Metal 驱动 .mm（Objective-C++）源
file(GLOB DRIVER_MM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
add_library(metal_driver_obj OBJECT ${DRIVER_MM_SOURCES})

# 为 Objective-C++ 源启用 C++20（等价于 SCons 中移除 -std=gnu++17 并添加 -std=c++20）
if(NOT MSVC) # MSVC 不处理 .mm
    target_compile_options(metal_driver_obj PRIVATE -std=c++20)
endif()

# 让 .mm 也能包含到第三方头
target_include_directories(metal_driver_obj PRIVATE
        "${THIRDPARTY_DIR}"
        "${THIRDPARTY_DIR}/include"
)

# 构建顺序上让驱动依赖第三方对象（贴近 SCons 的 Depends 语义）
add_dependencies(metal_driver_obj spirv_cross_obj)

# 将对象并入上层已存在的目标 godot（不创建新可执行 / 库）
target_sources(godot PRIVATE
        $<TARGET_OBJECTS:spirv_cross_obj>
        $<TARGET_OBJECTS:metal_driver_obj>
)