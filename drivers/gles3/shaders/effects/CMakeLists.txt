file(GLOB LOCAL_SOURCES "*.cpp")

# 直接把这些源附加到可执行目标 godot
target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
)

# --- 下面新增：GLSL -> .gen.h 的生成逻辑 ---

# 如使用 Python 版的编译脚本，请启用这一行；若使用其它可执行工具可删除
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# 1) 查找 *_inc.glsl 作为包含文件
file(GLOB GLSL_INCLUDE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*_inc.glsl"
)

# 2) 查找当前目录的所有 .glsl，并排除 *_inc.glsl
file(GLOB GLSL_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.glsl"
)
list(REMOVE_ITEM GLSL_FILES ${GLSL_INCLUDE_FILES})

# 3) 为每个 .glsl 定义一个 .gen.h 的生成命令，并声明对 include 文件和构建脚本的依赖
set(GENERATED_GLSL_HEADERS "")
foreach(glsl ${GLSL_FILES})
    get_filename_component(glsl_name "${glsl}" NAME)
    set(out_header "${CMAKE_CURRENT_BINARY_DIR}/${glsl_name}.gen.h")

    # 如果你有自定义的 shader 编译器，请把下面 COMMAND 部分替换为你的真实调用
    add_custom_command(
            OUTPUT "${out_header}"
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/gles3_builders.py" "${glsl}" "${out_header}"
            DEPENDS "${glsl}" ${GLSL_INCLUDE_FILES} "${CMAKE_SOURCE_DIR}/gles3_builders.py"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Compiling GLSL ${glsl_name} -> ${out_header}"
            VERBATIM
    )

    list(APPEND GENERATED_GLSL_HEADERS "${out_header}")
endforeach()

# 4) 将生成任务绑定到一个自定义 target，并让 godot 依赖它，还把生成的头加入到目标与包含路径
if(GENERATED_GLSL_HEADERS)
    # 给每个目录生成独立的 target 名，避免重名
    string(MD5 _dir_hash "${CMAKE_CURRENT_SOURCE_DIR}")
    add_custom_target(gles3_shaders_${_dir_hash} DEPENDS ${GENERATED_GLSL_HEADERS})
    add_dependencies(godot gles3_shaders_${_dir_hash})

    target_sources(godot PRIVATE ${GENERATED_GLSL_HEADERS})
    target_include_directories(godot PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()