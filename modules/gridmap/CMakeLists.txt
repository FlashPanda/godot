# 子级 CMakeLists.txt：不声明 project()，仅汇总源码
# 等价于 SCons 中的 env_modules.add_source_files(env.modules_sources, ...)

# 若父级未提供，则提供一个默认开关
if(NOT DEFINED EDITOR_BUILD)
    option(EDITOR_BUILD "是否为编辑器构建（对应 env.editor_build）" ON)
endif()

# 收集本目录下的 *.cpp
file(GLOB LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 若为编辑器构建，则额外收集 editor/*.cpp
set(ALL_SOURCES ${LOCAL_SOURCES})
if(EDITOR_BUILD)
    file(GLOB EDITOR_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp")
    list(APPEND ALL_SOURCES ${EDITOR_SOURCES})
endif()

# 优先直接追加到已存在的目标 godot；否则上浮到父作用域以便父级统一处理
if(TARGET godot)
    if(ALL_SOURCES)
        target_sources(godot PRIVATE ${ALL_SOURCES})
    endif()
else()
    # 上浮变量，父级可在合适位置使用 add_library/add_executable 时注入
    set(MODULE_SOURCES ${MODULE_SOURCES} ${ALL_SOURCES} PARENT_SCOPE)
endif()