# 确保可用 Python3 解释器（用于运行生成脚本）
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# 生成的源码路径放在构建目录中，保持源码树整洁
set(GEN_SRC ${CMAKE_CURRENT_BINARY_DIR}/default_controller_mappings.gen.cpp)

# 按顺序声明数据库文件（顺序很重要）
set(CONTROLLER_DATABASES
        ${CMAKE_CURRENT_SOURCE_DIR}/gamecontrollerdb.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/godotcontrollerdb.txt
)

# 通过 Python 调用 input_builders.make_default_controller_mappings 生成源码
add_custom_command(
        OUTPUT ${GEN_SRC}
        COMMAND ${Python3_EXECUTABLE} -c
        "import sys; sys.path.insert(0, r'${CMAKE_CURRENT_SOURCE_DIR}'); \
             import input_builders; \
             input_builders.make_default_controller_mappings([r'${GEN_SRC}'], \
                [r'${CMAKE_CURRENT_SOURCE_DIR}/gamecontrollerdb.txt', r'${CMAKE_CURRENT_SOURCE_DIR}/godotcontrollerdb.txt'], \
                None)"
        DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/input_builders.py
        ${CONTROLLER_DATABASES}
        COMMENT "Generating ${GEN_SRC}"
        VERBATIM
)

# 可选：单独的代码生成目标，便于显式依赖或手动触发
add_custom_target(input_codegen
        DEPENDS ${GEN_SRC}
)

# 将生成文件和本目录 *.cpp 源加入 godot 目标
file(GLOB LOCAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
        ${GEN_SRC}
)

# 确保在编译 godot 前完成生成步骤
add_dependencies(godot input_codegen)