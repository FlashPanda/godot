# 允许上层传入构建命令（解释器 + 脚本），例如：
#   set(GLES3_BUILDERS_CMD "${Python3_EXECUTABLE};${CMAKE_SOURCE_DIR}/modules/gles3/gles3_builders.py")
# 如果未传入，则给出友好提示。
if(NOT DEFINED GLES3_BUILDERS_CMD)
    message(WARNING "GLES3_BUILDERS_CMD 未设置。请在上层 CMake 中提供用于将 .glsl 生成 .gen.h 的命令（可为解释器+脚本），否则下面的自定义命令将无法生效。")
endif()

# 收集包含与源码着色器
file(GLOB GLSL_INCLUDE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*_inc.glsl")
file(GLOB GLSL_ALL_FILES     "${CMAKE_CURRENT_SOURCE_DIR}/*.glsl")

# 按 SCsub 的做法：目前显式列出需要编译的着色器
set(GLSL_SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/canvas.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/feed.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/scene.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/sky.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/canvas_occlusion.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/canvas_sdf.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/particles.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/particles_copy.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/skeleton.glsl"
)

# 如果将来要改为“全部 .glsl（排除 *_inc.glsl）”，可以用下面几行替换上面的显式列表：
# set(GLSL_SOURCE_FILES ${GLSL_ALL_FILES})
# if(GLSL_INCLUDE_FILES)
#     list(REMOVE_ITEM GLSL_SOURCE_FILES ${GLSL_INCLUDE_FILES})
# endif()

# 生成的头文件输出到当前二进制目录
set(GLSL_GEN_HEADERS "")
foreach(_glsl ${GLSL_SOURCE_FILES})
    get_filename_component(_name_we "${_glsl}" NAME_WE)
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/${_name_we}.gen.h")

    # 自定义命令：当源码 glsl、包含 glsl、以及构建器脚本变化时，重新生成
    add_custom_command(
            OUTPUT  "${_out}"
            # 这里假设 GLES3_BUILDERS_CMD 接受输入与输出参数。请按你的实际脚本接口调整参数名。
            COMMAND ${GLES3_BUILDERS_CMD} "${_glsl}" "${_out}"
            DEPENDS "${_glsl}" ${GLSL_INCLUDE_FILES} ${GLES3_BUILDERS_CMD}
            BYPRODUCTS "${_out}"
            COMMENT "Generating ${_name_we}.gen.h from ${_name_we}.glsl"
            VERBATIM
    )

    list(APPEND GLSL_GEN_HEADERS "${_out}")
endforeach()

# 聚合目标：生成所有着色器头
add_custom_target(gles3_shaders ALL
        DEPENDS ${GLSL_GEN_HEADERS}
)

# 假设上层已定义可执行目标 godot，这里把依赖和包含路径连接起来
# 若本子目录在 godot 之后添加，确保此处可见目标；否则请在上层 add_dependencies 关联。
if(TARGET godot)
    add_dependencies(godot gles3_shaders)
    target_include_directories(godot PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# 继续构建子目录 effects（对应 SConscript("effects/SCsub")）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/effects/CMakeLists.txt")
    add_subdirectory(effects)
endif()