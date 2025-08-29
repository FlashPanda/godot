# CMake 子目录配置（xatlas_unwrap 模块）
# 等价于 SCons SCsub 的构建逻辑：
# - 始终编译本目录下的 *.cpp 并合入父目标 godot；
# - 当启用内置 xatlas 时，编译 thirdparty/xatlas/xatlas.cpp，并添加其包含目录；
# - 第三方源码关闭编译告警（与 SCons 的 disable_warnings() 等价）。

# ----------------------------------------
# 选项：是否启用内置 xatlas（对应 env["builtin_xatlas"]）
# 默认启用，可由上层 -DXATLAS_UNWRAP_BUILTIN_XATLAS=OFF 关闭
# ----------------------------------------
option(XATLAS_UNWRAP_BUILTIN_XATLAS "Build xatlas_unwrap with bundled thirdparty/xatlas" ON)

# ----------------------------------------
# 本模块源文件（*.cpp）
# ----------------------------------------
file(GLOB XATLAS_UNWRAP_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# ----------------------------------------
# 内置 xatlas 第三方库
# thirdparty 源与包含目录
# ----------------------------------------
set(_XATLAS_TP_SOURCES "")
set(_XATLAS_TP_INCLUDE_DIRS "")
if(XATLAS_UNWRAP_BUILTIN_XATLAS)
    set(_XATLAS_TP_DIR "${CMAKE_SOURCE_DIR}/thirdparty/xatlas")
    list(APPEND _XATLAS_TP_INCLUDE_DIRS "${_XATLAS_TP_DIR}")

    # 目前 SCsub 仅编译一个源文件：xatlas.cpp
    list(APPEND _XATLAS_TP_SOURCES
            "${_XATLAS_TP_DIR}/xatlas.cpp"
    )

    # 关闭第三方源码告警（与 env_thirdparty.disable_warnings() 对齐）
    if(MSVC)
        set_source_files_properties(${_XATLAS_TP_SOURCES} PROPERTIES COMPILE_OPTIONS "/w")
    else()
        set_source_files_properties(${_XATLAS_TP_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endif()

# ----------------------------------------
# 将所有配置应用到目标
# 若父级已创建目标 godot，则直接应用；
# 否则将信息上浮到父级，由父级在创建目标后统一应用。
# ----------------------------------------
if(TARGET godot)
    # 源文件并入目标
    if(XATLAS_UNWRAP_LOCAL_SOURCES)
        target_sources(godot PRIVATE ${XATLAS_UNWRAP_LOCAL_SOURCES})
    endif()
    if(_XATLAS_TP_SOURCES)
        target_sources(godot PRIVATE ${_XATLAS_TP_SOURCES})
    endif()

    # 第三方头路径
    if(_XATLAS_TP_INCLUDE_DIRS)
        target_include_directories(godot PRIVATE ${_XATLAS_TP_INCLUDE_DIRS})
    endif()
else()
    # 上浮：源文件
    set(MODULE_SOURCES ${MODULE_SOURCES} ${XATLAS_UNWRAP_LOCAL_SOURCES} PARENT_SCOPE)
    set(MODULE_SOURCES ${MODULE_SOURCES} ${_XATLAS_TP_SOURCES} PARENT_SCOPE)

    # 上浮：包含目录（供父级统一应用）
    set(MODULE_INCLUDE_DIRS ${MODULE_INCLUDE_DIRS} ${_XATLAS_TP_INCLUDE_DIRS} PARENT_SCOPE)
endif()