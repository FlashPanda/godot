# 本文件为子目录的 CMake 构建脚本，不在此处设置 cmake_minimum_required()/project()。
# 目标：复刻同目录 SCsub 的逻辑：
#  - 收集当前目录下所有 *.cpp 源文件并加入上层目标
#  - 递归进入 dummy、renderer_rd、storage 子目录

# 可配置项：上层已创建的目标名（例如 add_executable(godot ...) 或 add_library(godot ...)）
set(GODOT_TARGET "godot" CACHE STRING "上层已创建的构建目标名称")

if(NOT TARGET ${GODOT_TARGET})
    message(FATAL_ERROR "未找到构建目标 ${GODOT_TARGET}。请在上层先创建该目标，或通过 -DGODOT_TARGET=... 指定。")
endif()

# 1) 收集当前目录下所有 C++ 源文件，并添加到上层目标
file(GLOB LOCAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
if(LOCAL_SOURCES)
    target_sources(${GODOT_TARGET} PRIVATE ${LOCAL_SOURCES})
endif()

# 2) 递归处理子目录（对应 SConscript("dummy/SCsub")、"renderer_rd/SCsub"、"storage/SCsub"）
foreach(_subdir IN ITEMS dummy renderer_rd storage)
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_subdir}")
        add_subdirectory(${_subdir})
    endif()
endforeach()