# 本文件为“子目录”的构建脚本，不在此处设置 cmake_minimum_required()/project()。
# 作用：复刻 SCons 脚本中 `env.add_source_files(env.servers_sources, "*.cpp")` 的效果，
#      即把当前目录下的所有 *.cpp 源文件附加到上层已有的构建目标中一起编译。

# 可配置项：上层已创建的构建目标名称（如 add_executable(godot ...) 或 add_library(godot ...)）
# 如需变更目标名，可在上层或命令行传入：-DGODOT_TARGET=your_target_name
set(GODOT_TARGET "godot" CACHE STRING "上层已创建的构建目标名称")

# 确认上层目标已存在，避免静默丢失源文件
if(NOT TARGET ${GODOT_TARGET})
    message(FATAL_ERROR
            "未找到构建目标 ${GODOT_TARGET}。请先在上层 CMake 中创建该目标，或通过 -DGODOT_TARGET=... 指定。"
    )
endif()

# 收集当前目录下的所有 .cpp 源文件
# 说明：
#  - 使用 GLOB 便于与 SCons 的通配行为对应；
#  - 若你的工程更偏好“显式列举文件”，可以改为手动维护列表。
file(GLOB LOCAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 将这些源文件附加到上层目标
# 说明：
#  - 使用 PRIVATE 作用域，表示这些源只在该目标内部使用；
#  - 若需要跨目标共享，可调整为 PUBLIC/INTERFACE（通常不需要）。
if(LOCAL_SOURCES)
    target_sources(${GODOT_TARGET} PRIVATE
            ${LOCAL_SOURCES}
    )
endif()

# 可选：如果需要让 IDE 在目标下显示头文件（非必需，仅用于浏览）
# file(GLOB LOCAL_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
# if(LOCAL_HEADERS)
#     target_sources(${GODOT_TARGET} PRIVATE ${LOCAL_HEADERS})
# endif()