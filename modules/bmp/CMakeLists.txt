# CMake

# 收集本目录下所有 .cpp 源文件
file(GLOB LOCAL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 将这些源文件添加到已存在的目标 godot（相当于追加到 modules 源）
target_sources(godot PRIVATE ${LOCAL_SOURCES})