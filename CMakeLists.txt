cmake_minimum_required(VERSION 4.0)
project(godot)

set(CMAKE_CXX_STANDARD 17)

# 收集main文件夹中的所有C++源文件
file(GLOB_RECURSE MAIN_SOURCES 
    "main/*.cpp"
)

# 收集main文件夹中的所有头文件
file(GLOB_RECURSE MAIN_HEADERS
    "main/*.h"
)

# 收集core文件夹中的所有C++源文件
file(GLOB_RECURSE CORE_SOURCES
    "core/*.cpp"
)

# 收集core文件夹中的所有头文件
file(GLOB_RECURSE CORE_HEADERS
    "core/*.h"
)

add_executable(godot ${CORE_SOURCES} ${MAIN_SOURCES})

# 添加包含目录，使得 #include "core/xxx" 能够正确找到文件
target_include_directories(godot PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# 输出 CMAKE_SOURCE_DIR 的内容
message(STATUS "CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")
