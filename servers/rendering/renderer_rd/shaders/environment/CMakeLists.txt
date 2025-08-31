# 本文件为子目录的构建脚本，不在此处设置 cmake_minimum_required()/project()。
# 目标：复刻 SCons 脚本逻辑：
#  - 收集 *_inc.glsl（本目录与上级目录）
#  - 收集当前目录下需要编译的 *.glsl（剔除 *_inc.glsl）
#  - 为每个 .glsl 生成对应的 <文件名>.glsl.gen.h（输出到当前二进制目录）
#  - 当任一 *_inc.glsl 或 glsl_builders.py 变化时，触发对应 .gen.h 的重新生成
#  - 将生成的头文件加入上层已有目标（默认目标名为 godot），并在编译前生成

# 可配置项：
#  1) 目标名（上层应已 add_executable()/add_library() 创建该目标）
set(GODOT_TARGET "godot" CACHE STRING "上层已创建的构建目标名称")
#  2) GLSL 生成器命令（等价于 SCons 的 env.RD_GLSL）
#     建议以“列表”的形式传入更稳妥，例如：
#       -DGODOT_RD_GLSL_TOOL="python;${PROJECT_SOURCE_DIR}/path/to/glsl_builders.py;--rd-glsl"
set(GODOT_RD_GLSL_TOOL "" CACHE STRING "用于将单个 .glsl 生成 .gen.h 的命令（可为 解释器;脚本;子命令 的列表）")
#  3) 指定输出文件的参数名（缺省为 -o），若生成器使用不同参数名可修改
set(GODOT_RD_GLSL_ARGS "-o" CACHE STRING "传递给生成器、用于指定输出文件路径的参数名")

if(NOT TARGET ${GODOT_TARGET})
    message(FATAL_ERROR "未找到构建目标 ${GODOT_TARGET}。请在上层先创建该目标，或通过 -DGODOT_TARGET=... 指定。")
endif()

if(NOT GODOT_RD_GLSL_TOOL)
    message(FATAL_ERROR
            "未设置 GODOT_RD_GLSL_TOOL。请在上层或命令行传入，例如：\n"
            "  -DGODOT_RD_GLSL_TOOL=\"python;${PROJECT_SOURCE_DIR}/path/to/glsl_builders.py;--rd-glsl\""
    )
endif()

# 1) 收集包含文件（本目录与上级目录）
file(GLOB GLSL_INCLUDE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*_inc.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/../*_inc.glsl"
)

# 2) 收集本目录下所有 .glsl，并剔除 *_inc.glsl（保持与 SCons 逻辑一致）
file(GLOB GLSL_ALL_IN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/*.glsl")
set(GLSL_SOURCES "${GLSL_ALL_IN_DIR}")
list(FILTER GLSL_SOURCES EXCLUDE REGEX "_inc\\.glsl$")

# 3) 可选依赖：glsl_builders.py（若存在，则加入触发依赖）
set(_CANDIDATE_BUILDER_PATHS
        "${PROJECT_SOURCE_DIR}/glsl_builders.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl_builders.py"
)
set(GODOT_GLSL_BUILDER_FILE "")
foreach(_p IN LISTS _CANDIDATE_BUILDER_PATHS)
    if(EXISTS "${_p}")
        set(GODOT_GLSL_BUILDER_FILE "${_p}")
        break()
    endif()
endforeach()

# 4) 为每个 .glsl 定义自定义命令，生成 <源名>.glsl.gen.h 到二进制目录
set(GENERATED_GLSL_HEADERS)
foreach(GLSL_FILE IN LISTS GLSL_SOURCES)
    get_filename_component(_glsl_name "${GLSL_FILE}" NAME)               # e.g. water.glsl
    set(_out_header "${CMAKE_CURRENT_BINARY_DIR}/${_glsl_name}.gen.h")   # e.g. <bin>/water.glsl.gen.h

    if(GODOT_GLSL_BUILDER_FILE)
        add_custom_command(
                OUTPUT  "${_out_header}"
                COMMAND ${GODOT_RD_GLSL_TOOL} "${GLSL_FILE}" ${GODOT_RD_GLSL_ARGS} "${_out_header}"
                DEPENDS "${GLSL_FILE}" ${GLSL_INCLUDE_FILES} "${GODOT_GLSL_BUILDER_FILE}"
                COMMENT "生成 GLSL 头文件: ${_out_header}"
                VERBATIM
        )
    else()
        add_custom_command(
                OUTPUT  "${_out_header}"
                COMMAND ${GODOT_RD_GLSL_TOOL} "${GLSL_FILE}" ${GODOT_RD_GLSL_ARGS} "${_out_header}"
                DEPENDS "${GLSL_FILE}" ${GLSL_INCLUDE_FILES}
                COMMENT "生成 GLSL 头文件: ${_out_header}"
                VERBATIM
        )
    endif()

    list(APPEND GENERATED_GLSL_HEADERS "${_out_header}")
endforeach()

# 5) 聚合目标：确保所有 .gen.h 会在构建时生成
add_custom_target(glsl_gen DEPENDS ${GENERATED_GLSL_HEADERS})

# 6) 将生成头文件与生成任务接入上层目标
if(GENERATED_GLSL_HEADERS)
    # 把生成的头文件声明为目标源，便于 IDE 可见与依赖跟踪
    target_sources(${GODOT_TARGET} PRIVATE ${GENERATED_GLSL_HEADERS})
    # 在编译主目标前先生成着色器头文件
    add_dependencies(${GODOT_TARGET} glsl_gen)
endif()
# 让编译器能 #include "<name>.glsl.gen.h"
target_include_directories(${GODOT_TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")