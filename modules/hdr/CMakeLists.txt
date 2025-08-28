# 不声明 project()，作为子目录使用
# 等价于：env_modules.add_source_files(env.modules_sources, "*.cpp")

# 收集当前目录下的所有 .cpp
file(GLOB LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 上浮给父作用域，父级应在合适位置使用 MODULE_SOURCES 构建目标
set(MODULE_SOURCES ${MODULE_SOURCES} ${LOCAL_SOURCES} PARENT_SCOPE)