# 本文件为子目录的构建脚本，不在此设置 cmake_minimum_required()/project()。
# 目标：复刻 SCons 脚本的逻辑：
#  - 收集 *_inc.glsl（包含文件）
#  - 收集本目录下要编译的 *.glsl（剔除 *_inc.glsl）
#  - 为每个 .glsl 生成对应的 .gen.h（位于当前二进制目录）
#  - 当任一 *_inc.glsl 或 glsl_builders.py 变更时，触发对应 .gen.h 的重新生成
#  - 将生成的头文件加入上层已有目标（假定目标名为 godot），并确保编译前生成
#
# 注意：
#  1) 上层 CMake 必须已经创建了名为 godot 的目标：add_executable(godot ...) 或 add_library(godot ...)
#  2) 需要提供“GLSL 生成器”命令（相当于 SCons 的 env.RD_GLSL），通过变量 GODOT_RD_GLSL_TOOL 指定
#     - 该命令应当接受输入 GLSL 文件与输出头文件路径（默认以参数 -o 指定输出，可通过 GODOT_RD_GLSL_ARGS 覆盖）
#     - 例如：python <path>/glsl_builders.py --rd-glsl <in.glsl> -o <out.gen.h>
#  3) 若你的命令行不同，请在上层修改 GODOT_RD_GLSL_ARGS 或者直接在 COMMAND 行自定义

# 可配置项：GLSL 生成器可执行命令与其额外参数
set(GODOT_RD_GLSL_TOOL "" CACHE STRING "用于将单个 .glsl 编译/生成到 .gen.h 的命令（可包含解释器+脚本）")
set(GODOT_RD_GLSL_ARGS "-o" CACHE STRING "传递给生成器、用于指定输出文件的参数（位于输出路径之前）")

if(NOT GODOT_RD_GLSL_TOOL)
    message(FATAL_ERROR
            "未设置 GODOT_RD_GLSL_TOOL。请在上层 CMake 或命令行传入，例如：\n"
            "  -DGODOT_RD_GLSL_TOOL=\"python;${PROJECT_SOURCE_DIR}/path/to/glsl_builders.py;--rd-glsl\""
    )
endif()

# 1) 收集包含文件（同目录与上级目录）
file(GLOB GLSL_INCLUDE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*_inc.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/../*_inc.glsl"
)

# 2) 收集本目录下所有 .glsl，并剔除 *_inc.glsl（SCons: glsl_files = Glob('*.glsl') if not in include）
file(GLOB GLSL_ALL_IN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/*.glsl")
# 直接用正则过滤比 list(REMOVE_ITEM) 更稳妥（避免因路径不完全一致导致删除失败）
set(GLSL_SOURCES "${GLSL_ALL_IN_DIR}")
list(FILTER GLSL_SOURCES EXCLUDE REGEX "_inc\\.glsl$")

# 3) glsl_builders.py 依赖（若存在则加入依赖触发；不存在时忽略）
#    这里尝试在工程根和本目录查找，可按需要扩展搜索路径
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

# 4) 为每个 .glsl 定义一个自定义命令，产出 .gen.h，并建立依赖关系
set(GENERATED_GLSL_HEADERS)
foreach(GLSL_FILE IN LISTS GLSL_SOURCES)
    # 生成文件名：<源文件名>.gen.h（输出到二进制目录，避免污染源码树）
    get_filename_component(_glsl_name "${GLSL_FILE}" NAME)            # e.g. my_shader.glsl
    set(_out_header "${CMAKE_CURRENT_BINARY_DIR}/${_glsl_name}.gen.h") # e.g. <bin>/my_shader.glsl.gen.h

    if(GODOT_GLSL_BUILDER_FILE)
        add_custom_command(
                OUTPUT "${_out_header}"
                COMMAND ${GODOT_RD_GLSL_TOOL} "${GLSL_FILE}" ${GODOT_RD_GLSL_ARGS} "${_out_header}"
                DEPENDS
                "${GLSL_FILE}"
                ${GLSL_INCLUDE_FILES}
                "${GODOT_GLSL_BUILDER_FILE}"
                COMMENT "生成 GLSL 头文件: ${_out_header}"
                VERBATIM
        )
    else()
        # 若未找到 glsl_builders.py，则不把它加入 DEPENDS（保持可用性）
        add_custom_command(
                OUTPUT "${_out_header}"
                COMMAND ${GODOT_RD_GLSL_TOOL} "${GLSL_FILE}" ${GODOT_RD_GLSL_ARGS} "${_out_header}"
                DEPENDS
                "${GLSL_FILE}"
                ${GLSL_INCLUDE_FILES}
                COMMENT "生成 GLSL 头文件: ${_out_header}"
                VERBATIM
        )
    endif()

    list(APPEND GENERATED_GLSL_HEADERS "${_out_header}")
endforeach()

# 5) 聚合目标：确保所有 .gen.h 会在构建时生成
#    - glsl_gen: 一个中间目标，专门负责驱动生成步骤
add_custom_target(glsl_gen DEPENDS ${GENERATED_GLSL_HEADERS})

# 6) 将生成头文件与生成任务接入上层目标（假定目标名为 godot）
#    - 把生成的头文件声明为该目标的源，便于 IDE 可见和依赖追踪
#    - 在编译 godot 前，先执行 glsl_gen
#    - 把二进制目录加入包含路径，便于 #include "<xx>.gen.h"
target_sources(godot PRIVATE ${GENERATED_GLSL_HEADERS})
add_dependencies(godot glsl_gen)
target_include_directories(godot PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# 额外说明：
# - 当任一 *_inc.glsl 或 glsl_builders.py 变化，依赖的 .glsl 输出会被重新生成；
# - 若你的生成器命令需要额外的依赖（如其它脚本或配置），可把它们追加到 add_custom_command 的 DEPENDS；
# - 若你的生成器命令行不采用 “<in> -o <out>” 形式，可通过设置 GODOT_RD_GLSL_ARGS 或直接修改上方 COMMAND 以适配。