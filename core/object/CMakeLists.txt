# ... existing code ...
file(GLOB LOCAL_SOURCES "*.cpp")

# 直接把这些源附加到可执行目标 godot
target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
)

# 生成 gdvirtual.gen.inc（保持与 SCons 相同：输出在源码目录）
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/gdvirtual.gen.inc
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/make_virtuals.py
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/make_virtuals.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating gdvirtual.gen.inc via make_virtuals.py"
        VERBATIM
)

# 封装成 target，便于与 godot 建立依赖
add_custom_target(gdvirtual_gen
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gdvirtual.gen.inc
)

# 确保在构建 godot 之前生成头文件
add_dependencies(godot gdvirtual_gen)

# 将生成的头文件加入构建依赖（便于增量构建追踪到变更）
target_sources(godot PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/gdvirtual.gen.inc
)
# ... existing code ...