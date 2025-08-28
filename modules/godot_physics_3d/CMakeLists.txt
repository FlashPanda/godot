# 子级 CMakeLists.txt：不声明 project()，仅汇总源码并递归子目录

# 1) 收集当前目录下的 *.cpp，并追加到父作用域的 MODULE_SOURCES 变量
#    父级应在合适位置使用该变量创建静态库/目标（等价于 SCons 中的 env.modules_sources 聚合）
file(GLOB LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 将本目录源文件上浮到父作用域
set(MODULE_SOURCES ${MODULE_SOURCES} ${LOCAL_SOURCES} PARENT_SCOPE)

# 2) 处理 joints 子目录
#    优先 add_subdirectory，让 joints 自己决定如何继续上浮；
#    如无 CMakeLists.txt，则回退：直接把 joints/*.cpp 也并入父作用域的 MODULE_SOURCES。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/joints/CMakeLists.txt")
    add_subdirectory(joints)
else()
    file(GLOB JOINTS_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/joints/*.cpp"
    )
    set(MODULE_SOURCES ${MODULE_SOURCES} ${JOINTS_SOURCES} PARENT_SCOPE)
endif()