# 确保可用 Python3 解释器
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# 1) 生成 ext_wrappers.gen.inc
set(WRAPPERS_INC ${CMAKE_CURRENT_BINARY_DIR}/ext_wrappers.gen.inc)

add_custom_command(
        OUTPUT ${WRAPPERS_INC}
        COMMAND ${Python3_EXECUTABLE} -c
        "import sys, os; sys.path.insert(0, r'${CMAKE_CURRENT_SOURCE_DIR}');
             import make_wrappers; make_wrappers.run([r'${WRAPPERS_INC}'],
             [r'${CMAKE_CURRENT_SOURCE_DIR}/make_wrappers.py'], None)"
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/make_wrappers.py
        COMMENT "Generating ${WRAPPERS_INC}"
        VERBATIM
)

# 2) 生成 gdextension_interface_dump.gen.h
set(GDEXT_DUMP ${CMAKE_CURRENT_BINARY_DIR}/gdextension_interface_dump.gen.h)

add_custom_command(
        OUTPUT ${GDEXT_DUMP}
        COMMAND ${Python3_EXECUTABLE} -c
        "import sys, os; sys.path.insert(0, r'${CMAKE_CURRENT_SOURCE_DIR}');
             import make_interface_dumper; make_interface_dumper.run([r'${GDEXT_DUMP}'],
             [r'${CMAKE_CURRENT_SOURCE_DIR}/gdextension_interface.h',
              r'${CMAKE_CURRENT_SOURCE_DIR}/make_interface_dumper.py'], None)"
        DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/make_interface_dumper.py
        ${CMAKE_CURRENT_SOURCE_DIR}/gdextension_interface.h
        COMMENT "Generating ${GDEXT_DUMP}"
        VERBATIM
)

# 聚合一个 codegen 目标，便于与 godot 建立依赖
add_custom_target(gdextension_codegen
        DEPENDS ${WRAPPERS_INC} ${GDEXT_DUMP}
)

# 3) 将生成目录加入包含路径，供源码包含 .gen.h/.gen.inc
target_include_directories(godot PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
)

# 4) 将生成物加入到目标的 sources 列表（便于 IDE 展示和依赖追踪）
target_sources(godot PRIVATE
        ${WRAPPERS_INC}
        ${GDEXT_DUMP}
)

# 5) 将本目录下 *.cpp 编译进 godot
file(GLOB LOCAL_SOURCES "*.cpp")
# ... existing code ...
target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
)

# 6) 确保在构建 godot 前完成代码生成
add_dependencies(godot gdextension_codegen)