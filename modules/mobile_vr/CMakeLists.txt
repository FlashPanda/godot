# CMake

# 说明：
# 本子目录的 CMakeLists.txt 对应 SCons 中：
#   env_mobile_vr = env_modules.Clone()
#   env_mobile_vr.add_source_files(env.modules_sources, "*.cpp")
# 作用是：收集当前目录下所有 *.cpp 文件，并把它们“追加”到父目录维护的
# 模块源文件列表变量中（这里命名为 MODULES_SOURCES）。
#
# 使用方式（示例）：
# 在父目录的 CMakeLists.txt 中：
#   set(MODULES_SOURCES "")              #（父目录先声明/初始化变量）
#   add_subdirectory(path/to/this/subdir)
#   # 之后 MODULES_SOURCES 会包含本子目录收集到的 *.cpp
#   add_library(servers ${MODULES_SOURCES})  # 或者追加到你的目标中
#
# 注意：
# - 这里使用 file(GLOB ...) 收集源文件，以达到与 "*.cpp" 匹配的效果。
# - 通过 PARENT_SCOPE 把更新后的列表“返回”给父作用域，模拟向 env.modules_sources 追加。
# - 如果你不希望使用 GLOB（例如更严格的构建可重复性），可以手动列出源文件并同样 set(... PARENT_SCOPE)。

# 收集当前目录下的所有 *.cpp 源文件
file(GLOB LOCAL_MODULE_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 可选：去重，防止多次包含相同源文件（在多子目录聚合时有用）
if(LOCAL_MODULE_SOURCES)
    list(REMOVE_DUPLICATES LOCAL_MODULE_SOURCES)
endif()

# 将本目录收集到的源文件追加到父作用域的 MODULES_SOURCES 变量中
# 若父作用域尚未定义 MODULES_SOURCES，此处等价于直接“定义并赋值”
set(MODULES_SOURCES
        ${MODULES_SOURCES}         # 父作用域已存在的列表（若无则为空）
        ${LOCAL_MODULE_SOURCES}    # 本目录新增的源文件
        PARENT_SCOPE)

# 可选：如果需要，你也可以把当前目录暴露为包含目录，供父作用域目标使用（默认与 SCsub 等价逻辑无须设置）
# set(MODULES_INCLUDE_DIRS
#     ${MODULES_INCLUDE_DIRS}
#     ${CMAKE_CURRENT_SOURCE_DIR}
#     PARENT_SCOPE)

# 说明：
# 没有在此处直接创建库/目标，是为了保持与 SCsub 的行为一致——仅把源文件交给上层统一汇总与构建。