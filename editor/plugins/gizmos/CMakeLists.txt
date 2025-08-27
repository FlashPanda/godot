# 子级 CMakeLists：不声明 project()，仅组织本目录与子目录源码

# 收集当前目录下的 *.cpp
file(GLOB LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 将这些源文件附加到上层已定义的目标 godot
# 注意：确保上层已定义 add_executable(godot ...) 或 add_library(godot ...)
target_sources(godot PRIVATE ${LOCAL_SOURCES})
