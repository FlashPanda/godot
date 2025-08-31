# 本文件为“子目录”的 CMake 构建脚本：
# 目标：将当前目录下的所有 *.cpp 源文件加入到上层已存在的目标中
# 使用方法：
#   - 在上层 CMakeLists.txt 中先创建目标（add_executable 或 add_library）
#   - 通过 add_subdirectory(...) 引入本子目录
#   - 可通过 -DGODOT_TARGET=你的目标名 指定要追加源文件的目标（默认 godot）

# 可配置项：上层已创建的构建目标名称
set(GODOT_TARGET "godot" CACHE STRING "上层已创建的构建目标名称（如 godot）")

# 基本校验：要求上层已创建该目标
if(NOT TARGET ${GODOT_TARGET})
    message(FATAL_ERROR
            "未找到构建目标 ${GODOT_TARGET}。请在上层先创建该目标，或通过 -DGODOT_TARGET=... 指定。")
endif()

# 1) 收集当前目录下的所有 .cpp 源文件
file(GLOB LOCAL_CPP_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 2) 将这些源文件追加到上层目标
if(LOCAL_CPP_SOURCES)
    target_sources(${GODOT_TARGET} PRIVATE ${LOCAL_CPP_SOURCES})

    # 可选：为 IDE 按目录树分组，便于浏览（不会影响编译）
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${LOCAL_CPP_SOURCES})
endif()

# 说明：
# - 本脚本仅负责把本目录下的 *.cpp 加入到指定目标，不额外添加包含目录或编译选项。
# - 如果需要同时加入本目录为包含路径，可取消下行注释：
#   target_include_directories(${GODOT_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")