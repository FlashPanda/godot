# 本文件为子目录的构建脚本，不设置 cmake_minimum_required/project。

# 1) 第三方源码（预留，当前为空；如后续有 thirdparty 源，把路径填入此变量即可）
set(THIRDPARTY_SRCS)

# 若需要为第三方源码关闭编译告警，可在此启用（当前列表为空不会产生影响）
if(THIRDPARTY_SRCS)
    if(MSVC)
        set_source_files_properties(${THIRDPARTY_SRCS} PROPERTIES COMPILE_OPTIONS "/w")
    else()
        set_source_files_properties(${THIRDPARTY_SRCS} PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endif()

# 2) 本目录内的 Godot 源文件（对应 env.add_source_files(scene_obj, "*.cpp")）
file(GLOB LOCAL_SOURCES "*.cpp")

# 3) 将源文件加入到上层已定义的可执行/库目标 godot
#    注：需要确保上层 CMake 已经创建了名为 godot 的目标。
target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
        ${THIRDPARTY_SRCS}
)

# 4) 依赖关系说明：
#    SCons 中的 env.Depends(scene_obj, thirdparty_obj) 会在第三方代码更新时强制重建 scene 对象。
#    CMake 下若 thirdparty 源也属于同一目标（如本例），编译器和构建系统会自动追踪依赖，
#    不需要额外声明显式 Depends。当前 THIRDPARTY_SRCS 为空，不产生任何额外依赖。