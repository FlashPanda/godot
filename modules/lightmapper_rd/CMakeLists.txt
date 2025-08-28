cmake_minimum_required(VERSION 3.20)

# 需要 python3 来驱动 shader 头文件生成（对应 SCons 的 GLSL_HEADER）
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# 1) 要生成头的 GLSL 源列表（与 SCsub 一致）
set(_LM_GLSL_LIST
        lm_raster.glsl
        lm_compute.glsl
        lm_blendseams.glsl
)

# 2) 公共依赖（对应 SCsub 的 Depends）
set(_LM_EXTRA_DEPS
        ${CMAKE_CURRENT_SOURCE_DIR}/lm_common_inc.glsl
        ${CMAKE_SOURCE_DIR}/glsl_builders.py
)

# 3) 为每个 GLSL 声明自定义生成命令，输出 *.glsl.gen.h 到二进制目录
set(_LM_GEN_HEADERS)
foreach(_src ${_LM_GLSL_LIST})
    get_filename_component(_stem ${_src} NAME)
    set(_out ${CMAKE_CURRENT_BINARY_DIR}/${_stem}.gen.h)

    add_custom_command(
            OUTPUT ${_out}
            COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/glsl_builders.py
            header
            ${CMAKE_CURRENT_SOURCE_DIR}/${_src}
            ${_out}
            DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/${_src}
            ${_LM_EXTRA_DEPS}
            COMMENT "Generating ${_out} from ${_src}"
            VERBATIM
    )

    list(APPEND _LM_GEN_HEADERS ${_out})
endforeach()

# 4) 聚合一个 codegen 目标，便于与 godot 建立依赖关系
add_custom_target(lightmapper_rd_codegen
        DEPENDS ${_LM_GEN_HEADERS}
)

# 5) 编译本目录下的 *.cpp（对应 env_lightmapper_rd.add_source_files("*.cpp")）
file(GLOB _LM_LOCAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 6) 将生成目录加入到 godot 的包含路径，并把生成物/源码加入到 godot 目标
target_include_directories(godot PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_sources(godot PRIVATE
        ${_LM_LOCAL_SOURCES}
        ${_LM_GEN_HEADERS}
)

# 7) 确保在构建 godot 前完成 shader 头的生成
add_dependencies(godot lightmapper_rd_codegen)