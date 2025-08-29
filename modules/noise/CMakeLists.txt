# 子目录 CMakeLists：noise 模块
# 功能等价于 SCsub：
# - 将 thirdparty/noise 目录加入包含路径（相当于 Prepend CPPPATH）
# - 收集本目录下 *.cpp 以及 editor/*.cpp 源文件
# - 将上述源码并入上层已定义的目标 godot

# 1) 确认上层已创建编译目标 godot（需在 add_subdirectory 之前定义）
if(NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Please add this subdirectory after defining the 'godot' target.")
endif()

# 2) thirdparty/noise 的包含路径
# 使用工程根目录（PROJECT_SOURCE_DIR/CMAKE_SOURCE_DIR）指向 thirdparty 目录
set(NOISE_THIRDPARTY_DIR "${PROJECT_SOURCE_DIR}/thirdparty/noise")

# 将 thirdparty/noise 作为私有包含路径加入 godot 目标
# 等价于：env_noise.Prepend(CPPPATH=[thirdparty_dir])
target_include_directories(godot PRIVATE
        "${NOISE_THIRDPARTY_DIR}"
)

# 3) 收集模块源码
# 等价于：
#   env_noise.add_source_files(module_obj, "*.cpp")
#   env_noise.add_source_files(module_obj, "editor/*.cpp")
file(GLOB MODULE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp"
)

# 4) 将模块源码并入 godot 目标
# 等价于：env.modules_sources += module_obj
if(MODULE_SOURCES)
    target_sources(godot PRIVATE ${MODULE_SOURCES})
endif()