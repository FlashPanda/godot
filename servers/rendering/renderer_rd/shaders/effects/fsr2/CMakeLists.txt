# 本文件为子目录 CMake 构建脚本，不在此处设置 cmake_minimum_required()/project()。
# 目标：复刻 SCons 脚本逻辑：
#  - 收集所有包含用的 *_inc.glsl（本目录与上级目录）
#  - 额外把 FSR2 的 .h/.glsl（thirdparty/amd-fsr2/shaders）作为“包含依赖”加入
#  - 收集当前目录下需要编译的 *.glsl（剔除包含用 *_inc.glsl 与 FSR2 glsl）
#  - 为每个 *.glsl 生成对应 <源名>.glsl.gen.h（输出到当前二进制目录）
#  - 当任一 *_inc.glsl、FSR2 的 .h/.glsl 或 glsl_builders.py 发生变化时，触发对应 .gen.h 重新生成
#  - 将生成的头文件加入上层目标（假定目标名为 godot），并确保在编译前生成

# 可配置项：指定“GLSL 生成器”命令（等价于 SCons 的 env.RD_GLSL）
# 说明：
#   - GODOT_RD_GLSL_TOOL 为命令（可包含解释器与脚本），以 CMake 列表形式传入更稳妥：
#       例如 -DGODOT_RD_GLSL_TOOL="python;${PROJECT_SOURCE_DIR}/path/to/glsl_builders.py;--rd-glsl"
#   - GODOT_RD_GLSL_ARGS 是传给生成器用于指定输出文件的参数名，默认使用 "-o"
set(GODOT_RD_GLSL_TOOL "" CACHE STRING "用于将单个 .glsl 生成 .gen.h 的命令（可为解释器+脚本+子命令的列表）")
set(GODOT_RD_GLSL_ARGS "-o" CACHE STRING "传递给生成器、用于指定输出文件路径的参数名")

if(NOT GODOT_RD_GLSL_TOOL)
    message(FATAL_ERROR
            "未设置 GODOT_RD_GLSL_TOOL。请在上层 CMake 或命令行传入，例如：\n"
            "  -DGODOT_RD_GLSL_TOOL=\"python;${PROJECT_SOURCE_DIR}/path/to/glsl_builders.py;--rd-glsl\""
    )
endif()

# 1) 收集包含文件（本目录与上级目录的 *_inc.glsl）
file(GLOB GLSL_INCLUDE_FILES_LOCAL
        "${CMAKE_CURRENT_SOURCE_DIR}/*_inc.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/../*_inc.glsl"
)

# 2) 收集 FSR2 的头文件与 glsl，作为“包含依赖”
#    注意：这些文件仅作为依赖，不会被当作要编译的着色器输入
set(FSR2_SHADER_DIR "${PROJECT_SOURCE_DIR}/thirdparty/amd-fsr2/shaders")
if(EXISTS "${FSR2_SHADER_DIR}")
    file(GLOB FSR2_INCLUDE_FILES
            "${FSR2_SHADER_DIR}/*.h"
            "${FSR2_SHADER_DIR}/*.glsl"
    )
else()
    set(FSR2_INCLUDE_FILES "")
endif()

# 汇总所有“包含依赖”列表
set(ALL_INCLUDE_DEPS ${GLSL_INCLUDE_FILES_LOCAL} ${FSR2_INCLUDE_FILES})

# 3) 收集本目录下所有 .glsl，并剔除包含用 *_inc.glsl（以及 FSR2 的 glsl）
file(GLOB GLSL_ALL_IN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/*.glsl")
set(GLSL_COMPILE_SOURCES "${GLSL_ALL_IN_DIR}")

# 剔除 *_inc.glsl
list(FILTER GLSL_COMPILE_SOURCES EXCLUDE REGEX "_inc\\.glsl$")

# 由于 FSR2 的 glsl 不在本目录，通常不会出现在 GLSL_ALL_IN_DIR。若未来把其软链到本目录，可继续剔除：
if(FSR2_INCLUDE_FILES)
    # 构造一个仅含扩展名为 .glsl 的 FSR2 列表用于排除
    set(_FSR2_GLSL_ONLY "")
    foreach(_f IN LISTS FSR2_INCLUDE_FILES)
        if(_f MATCHES "\\.glsl$")
            list(APPEND _FSR2_GLSL_ONLY "${_f}")
        endif()
    endforeach()
    # 若列表非空，尝试按文件名进行排除（仅当文件名冲突到当前目录时才会有影响）
    if(_FSR2_GLSL_ONLY)
        foreach(_f IN LISTS _FSR2_GLSL_ONLY)
            get_filename_component(_fsr2_name "${_f}" NAME) # e.g. xxx.glsl
            list(FILTER GLSL_COMPILE_SOURCES EXCLUDE REGEX ".*/${_fsr2_name}$")
        endforeach()
    endif()
endif()

# 4) 可选依赖：glsl_builders.py（若存在，则加入触发依赖）
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

# 5) 为每个 .glsl 定义自定义命令，生成 <文件名>.glsl.gen.h 到二进制目录
set(GENERATED_GLSL_HEADERS)
foreach(GLSL_FILE IN LISTS GLSL_COMPILE_SOURCES)
    get_filename_component(_glsl_name "${GLSL_FILE}" NAME)                 # e.g. water.glsl
    set(_out_header "${CMAKE_CURRENT_BINARY_DIR}/${_glsl_name}.gen.h")     # e.g. <bin>/water.glsl.gen.h

    if(GODOT_GLSL_BUILDER_FILE)
        add_custom_command(
                OUTPUT  "${_out_header}"
                COMMAND ${GODOT_RD_GLSL_TOOL} "${GLSL_FILE}" ${GODOT_RD_GLSL_ARGS} "${_out_header}"
                DEPENDS "${GLSL_FILE}" ${ALL_INCLUDE_DEPS} "${GODOT_GLSL_BUILDER_FILE}"
                COMMENT "生成 GLSL 头文件: ${_out_header}"
                VERBATIM
        )
    else()
        add_custom_command(
                OUTPUT  "${_out_header}"
                COMMAND ${GODOT_RD_GLSL_TOOL} "${GLSL_FILE}" ${GODOT_RD_GLSL_ARGS} "${_out_header}"
                DEPENDS "${GLSL_FILE}" ${ALL_INCLUDE_DEPS}
                COMMENT "生成 GLSL 头文件: ${_out_header}"
                VERBATIM
        )
    endif()

    list(APPEND GENERATED_GLSL_HEADERS "${_out_header}")
endforeach()

# 6) 聚合目标：确保所有 .gen.h 会在构建时生成
add_custom_target(glsl_gen DEPENDS ${GENERATED_GLSL_HEADERS})

# 7) 把生成文件和生成任务接入上层目标（假定目标名为 godot）
#    - 将生成的头文件声明为目标源，便于 IDE 可见与依赖跟踪
#    - 在编译 godot 前，先执行 glsl_gen
#    - 把二进制目录加入包含路径，便于 #include "<name>.glsl.gen.h"
if(GENERATED_GLSL_HEADERS)
    target_sources(godot PRIVATE ${GENERATED_GLSL_HEADERS})
    add_dependencies(godot glsl_gen)
endif()
target_include_directories(godot PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# 提示：
# - 若你的生成器命令行并非 “<in> -o <out>”，请通过 GODOT_RD_GLSL_ARGS 调整，或直接修改上方 COMMAND。
# - 如果需要其他脚本/资源参与依赖触发，可把它们追加到 add_custom_command 的 DEPENDS 中。