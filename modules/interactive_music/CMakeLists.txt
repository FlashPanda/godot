# 子级 CMakeLists.txt：不声明 project()，仅收集并上报源码

# 若父级未提供，则给出一个默认开关，等价于 env.editor_build
if(NOT DEFINED EDITOR_BUILD)
    option(EDITOR_BUILD "是否为编辑器构建（对应 env.editor_build）" ON)
endif()

# 收集本目录下的源文件
file(GLOB LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

set(ALL_SOURCES ${LOCAL_SOURCES})

# 编辑器构建则额外包含 editor 目录下的源
if(EDITOR_BUILD)
    file(GLOB EDITOR_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp"
    )
    list(APPEND ALL_SOURCES ${EDITOR_SOURCES})
endif()

# 优先追加到已存在的目标 godot；否则上浮到父作用域由父级统一处理
if(TARGET godot)
    if(ALL_SOURCES)
        target_sources(godot PRIVATE ${ALL_SOURCES})
    endif()
else()
    # 父级可在合适位置使用 MODULE_SOURCES 构建目标或库
    set(MODULE_SOURCES ${MODULE_SOURCES} ${ALL_SOURCES} PARENT_SCOPE)
endif()