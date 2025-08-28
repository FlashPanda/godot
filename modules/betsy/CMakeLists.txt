# 子目录 CMakeLists.txt

# 需要 Python3 解释器来调用 glsl_builders.py
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# 1) 生成 GLSL 头文件（*.glsl.gen.h）
# 与 SCsub 中的 GLSL_HEADER("...") 列表保持一致
set(BETSY_GLSL_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/bc6h.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/bc1.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/bc4.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/alpha_stitch.glsl"
)

set(GENERATED_GLSL_HEADERS "")
foreach(GLSL_IN ${BETSY_GLSL_FILES})
    get_filename_component(GLSL_NAME "${GLSL_IN}" NAME)           # 例如 bc6h.glsl
    set(HEADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/${GLSL_NAME}.gen.h") # 例如 bc6h.glsl.gen.h

    add_custom_command(
            OUTPUT "${HEADER_OUT}"
            COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/glsl_builders.py"
            --header "${GLSL_IN}" "${HEADER_OUT}"
            DEPENDS "${GLSL_IN}" "${CMAKE_SOURCE_DIR}/glsl_builders.py"
            COMMENT "Generating ${HEADER_OUT} from ${GLSL_IN}"
            VERBATIM
    )

    list(APPEND GENERATED_GLSL_HEADERS "${HEADER_OUT}")
endforeach()

# 聚合生成任务，确保在编译前生成
add_custom_target(betsy_glsl_headers DEPENDS ${GENERATED_GLSL_HEADERS})

# 2) thirdparty 头文件路径（等价于 SCsub 的 Prepend CPPPATH）
target_include_directories(godot PRIVATE
        "${CMAKE_SOURCE_DIR}/thirdparty/betsy"
        "${CMAKE_CURRENT_BINARY_DIR}"   # 让编译器能找到生成的 *.glsl.gen.h
)

# 3) 当前目录下的 C++ 源文件加入到父目标 godot
file(GLOB LOCAL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
        ${GENERATED_GLSL_HEADERS}   # 将生成的头文件加入依赖图，保证先行生成
)

# 确保在构建 godot 前运行头文件生成
add_dependencies(godot betsy_glsl_headers)