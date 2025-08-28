# CMake

# 说明：
# 本子目录的 CMakeLists.txt 用于实现与 SCons/SCsub 等价的行为：
#   1) “配置 Mono”步骤：在 SCons 中为 mono_configure.configure(env, env_mono)。
#      这里不直接生成目标，仅负责收集源文件并把结果上交父作用域，因此仅提供“配置钩子”的位置与说明，
#      具体包含目录/宏/库等由父级统一目标去消费（见下文注释）。
#   2) 收集源文件并“追加”到父作用域的 MODULES_SOURCES：
#        *.cpp
#        glue/*.cpp
#        mono_gd/*.cpp
#        utils/*.cpp
#   3) 若开启编辑器构建（EDITOR_BUILD=ON），还会收集：
#        editor/*.cpp
#      并进入子目录 editor/script_templates（等价 SConscript("editor/script_templates/SCsub")）。
#
# 使用方式（示例）：
#   在父目录 CMakeLists.txt 中：
#     set(MODULES_SOURCES "")                       # 初始化父作用域收集变量
#     option(EDITOR_BUILD "Build editor features" ON)  # 控制是否启用编辑器代码
#     add_subdirectory(path/to/this/subdir)
#     # 之后 MODULES_SOURCES 会包含本子目录收集到的所有 .cpp
#     add_library(godot_modules ${MODULES_SOURCES}) # 父级统一创建目标
#     # 若你需要配置 Mono 的包含目录/宏/库，请在父级目标上设置：
#     #   target_include_directories(godot_modules PRIVATE ${MONO_INCLUDE_DIRS})
#     #   target_compile_definitions(godot_modules PRIVATE ${MONO_DEFINES})
#     #   target_link_libraries(godot_modules PRIVATE ${MONO_LIBS})
#
# 注意：
# - 我们使用 file(GLOB ...) + CONFIGURE_DEPENDS 模拟 "*.cpp" 样式匹配。
# - 通过 set(... PARENT_SCOPE) 把汇总后的列表“返回”父作用域，模拟追加到 env.modules_sources。
# - “Mono 配置”在此不直接修改编译器参数或创建目标，只保留对父级的可扩展变量约定：
#     MONO_INCLUDE_DIRS / MONO_DEFINES / MONO_LIBS
#   父级可自行决定如何在目标上消费这些变量。
# - 若存在 editor/script_templates 子目录，且开启 EDITOR_BUILD，将 add_subdirectory 进入它，
#   以实现与 SCsub 调用一致的生成流程（例如生成 templates.gen.h）。

# 开关：是否启用编辑器构建（上层可覆盖）
option(EDITOR_BUILD "Build editor sources for this module" OFF)

# -------------------------
# 1) Mono 配置（占位与说明）
# -------------------------
# 如果需要在此处探测或设置 Mono 相关变量，可在进入本子目录前的父 CMake 中完成，
# 例如：
#   set(MONO_INCLUDE_DIRS "...;...;...")
#   set(MONO_DEFINES "MONO_ENABLED;FOO=1")
#   set(MONO_LIBS "mono-2.0;...")
# 然后在父级目标里使用 target_* 指令应用这些变量。
# 本子目录不直接创建编译目标，保持与 SCsub 仅“收集源”的职责一致。

# -------------------------
# 2) 收集模块源文件
# -------------------------
file(GLOB LOCAL_MODULE_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/glue/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/mono_gd/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/utils/*.cpp")

# 可选：编辑器相关源文件（EDITOR_BUILD = ON 时包含）
if(EDITOR_BUILD)
    file(GLOB LOCAL_EDITOR_SOURCES
            CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp")
    list(APPEND LOCAL_MODULE_SOURCES ${LOCAL_EDITOR_SOURCES})
endif()

# 去重，避免多次聚合重复项
list(REMOVE_DUPLICATES LOCAL_MODULE_SOURCES)

# 将本目录收集到的源文件追加到父作用域的 MODULES_SOURCES 变量
set(MODULES_SOURCES
        ${MODULES_SOURCES}         # 父作用域已有的列表（若无则为空）
        ${LOCAL_MODULE_SOURCES}    # 本目录新增的源文件
        PARENT_SCOPE)

# 可选：将包含目录暴露给父作用域（如需要可启用）
# set(MODULES_INCLUDE_DIRS
#     ${MODULES_INCLUDE_DIRS}
#     ${CMAKE_CURRENT_SOURCE_DIR}
#     ${CMAKE_CURRENT_SOURCE_DIR}/glue
#     ${CMAKE_CURRENT_SOURCE_DIR}/mono_gd
#     ${CMAKE_CURRENT_SOURCE_DIR}/utils
#     $<$<BOOL:${EDITOR_BUILD}>:${CMAKE_CURRENT_SOURCE_DIR}/editor>
#     PARENT_SCOPE)

# -------------------------
# 3) 进入 editor/script_templates 子目录（仅在 EDITOR_BUILD 时）
# -------------------------
if(EDITOR_BUILD)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/editor/script_templates/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/editor/script_templates")
    endif()
endif()

# 说明：
# - 本文件不直接创建库/可执行文件，与 SCsub 一样只负责把源文件交给上层统一构建。
# - 如需在父级依赖由 script_templates 生成的头文件，可在父级 add_dependencies(...)
#   或 target_include_directories(...) 中引用该子目录暴露的变量/目标（视其实现而定）。