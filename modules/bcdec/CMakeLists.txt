# CMake

# 确保父作用域用于聚合模块源码的变量存在
if(NOT DEFINED GODOT_MODULES_SOURCES)
    set(GODOT_MODULES_SOURCES "" CACHE INTERNAL "" FORCE)
endif()

# 收集当前目录下所有 .cpp 源文件（等价于 add_source_files(env.modules_sources, "*.cpp")）
file(GLOB MODULE_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 追加到父作用域聚合变量，父级再统一 target_sources(godot ...) 使用
set(GODOT_MODULES_SOURCES
        ${GODOT_MODULES_SOURCES}
        ${MODULE_LOCAL_SOURCES}
        PARENT_SCOPE
)