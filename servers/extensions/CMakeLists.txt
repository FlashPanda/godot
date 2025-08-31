# 本文件为子目录的构建脚本，不在此处设置 cmake_minimum_required()/project()。
# 功能等价于 SCons 脚本：
#   - 始终编译 physics_server_2d_extension.cpp
#   - 当未禁用 3D（即 disable_3d 为 false）时，编译 physics_server_3d_extension.cpp

# 1) 允许从上层控制是否禁用 3D：
#    - 优先读取外部传入的 DISABLE_3D（兼容一些现有工程习惯）
#    - 提供 GODOT_DISABLE_3D 作为正式的 CMake 选项（默认 OFF，即不禁用 3D）
if(DEFINED DISABLE_3D AND NOT DEFINED GODOT_DISABLE_3D)
    # 若上层以 -DDISABLE_3D=ON 传入，这里复用到 GODOT_DISABLE_3D
    set(GODOT_DISABLE_3D "${DISABLE_3D}")
endif()
option(GODOT_DISABLE_3D "Disable building 3D physics extension sources" OFF)

# 2) 确保上层已创建目标（可执行或库）。如目标名不同，请把 godot 替换为你的实际目标名。
if(NOT TARGET godot)
    message(FATAL_ERROR "目标 'godot' 未在上层定义。请在顶层/父级 CMake 中先创建该目标，或在此文件中将 'godot' 替换为你的目标名。")
endif()

# 3) 始终加入 2D 源文件（对应：env_object.add_source_files(env.servers_sources, "physics_server_2d_extension.cpp")）
target_sources(godot PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/physics_server_2d_extension.cpp
)

# 4) 当未禁用 3D 时，加入 3D 源文件（对应：if not env["disable_3d"] ...）
if(NOT GODOT_DISABLE_3D)
    target_sources(godot PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/physics_server_3d_extension.cpp
    )
endif()

# 使用说明：
# - 若需要禁用 3D，可在配置时传入：
#     cmake -S . -B build -DGODOT_DISABLE_3D=ON
#   或（兼容方式）：
#     cmake -S . -B build -DDISABLE_3D=ON