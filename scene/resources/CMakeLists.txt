# 本文件为子目录的构建脚本，不在此处设置 cmake_minimum_required()/project()。
# 其职责是：
# 1) 收集当前目录 *.cpp 源文件并加入到上层已创建的目标（假定名称为 godot）。
# 2) 加入 thirdparty/misc 下的第三方 C 源码，同时对这些源码关闭编译告警。
# 3) 始终加入子目录 2d；通过选项控制是否加入 3d。

# -------- 选项：是否禁用 3D 构建（默认不禁用，开启 3D） --------
# 与 SCons 中的: if not env["disable_3d"]: ... 相对应
option(GODOT_DISABLE_3D "Disable building 3D scene sources in this directory" OFF)

# -------- 第三方源码（对应 SCsub 中的 thirdparty/misc/mikktspace.c 与 qoa.c）--------
# 注意：此处使用 CMAKE_SOURCE_DIR 指向仓库根目录，请确保本子目录是从项目顶层 add_subdirectory 引入的。
set(THIRDPARTY_SRCS
        "${CMAKE_SOURCE_DIR}/thirdparty/misc/mikktspace.c"
        "${CMAKE_SOURCE_DIR}/thirdparty/misc/qoa.c"
)

# 为第三方源码关闭告警（等价于 SCons 的 env_thirdparty.disable_warnings()）
if(THIRDPARTY_SRCS)
    if(MSVC)
        # MSVC：/w 关闭所有警告
        set_source_files_properties(${THIRDPARTY_SRCS} PROPERTIES COMPILE_OPTIONS "/w")
    else()
        # GCC/Clang：-w 关闭所有警告
        set_source_files_properties(${THIRDPARTY_SRCS} PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endif()

# -------- 本目录内的 Godot 源文件（对应 env.add_source_files(scene_obj, "*.cpp")）--------
file(GLOB LOCAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# -------- 将源文件加入到上层已定义的目标（假设为 godot）--------
# 需要确保上层 CMake 已经创建了名为 godot 的可执行或库目标（例如 add_executable(godot ...) 或 add_library(godot ...)）。
# 如目标名不同，请将 godot 替换为实际目标名。
target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
        ${THIRDPARTY_SRCS}
)

# 说明：SCons 中的 env.Depends(scene_obj, thirdparty_obj) 用于当第三方代码更新时强制重建 scene 对象。
# 在 CMake 中，若第三方源文件也属于同一目标（如本例），构建系统会自动根据源文件时间戳决定是否重编译，
# 无需手动声明显式依赖关系。

# -------- 子目录（对应 SConscript("2d/SCsub") 和条件性的 3d/SCsub）--------
add_subdirectory(2d)

if(NOT GODOT_DISABLE_3D)
    add_subdirectory(3d)
endif()