# 该 CMakeLists.txt 用于子目录，不在此处调用 cmake_minimum_required()/project()。

# ------------------------------------------------------------------------------
# 1) 收集当前目录下的所有 C++ 源文件
#    等价于：env.add_source_files(env.scene_sources, "*.cpp")
#    使用 CONFIGURE_DEPENDS 让 CMake 在源文件变动时自动重新配置（尽量接近 SCons 的行为）
# ------------------------------------------------------------------------------
file(GLOB CONFIGURE_DEPENDS SCENE_LOCAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# ------------------------------------------------------------------------------
# 2) 构建本目录的静态库 target：scene
#    对应 SCons 的：
#      lib = env.add_library("scene", env.scene_sources)
#    注意：子目录可能会往该目标追加源文件（见后续 add_subdirectory 和子目录的 target_sources 调用）
# ------------------------------------------------------------------------------
add_library(scene STATIC
        ${SCENE_LOCAL_SOURCES}
)

# 你可以在此添加需要的包含路径或编译定义（若有）
# target_include_directories(scene PUBLIC/PRIVATE ...)
# target_compile_definitions(scene PUBLIC/PRIVATE ...)

# ------------------------------------------------------------------------------
# 3) 递归“链式”包含子目录
#    对应 SCons 的：
#      SConscript("main/SCsub") 等
#    为了兼容性：仅在子目录真实存在时 add_subdirectory。
#    其中 3d 子目录可通过选项开关，等价于 `if not env["disable_3d"]`
# ------------------------------------------------------------------------------

# 3.1 开关：是否禁用 3D。默认不禁用（即构建 3d 子目录）
option(GODOT_DISABLE_3D "Disable 3D scene submodule" OFF)

# 3.2 链式包含（顺序尽量与原脚本保持一致）
foreach(_subdir IN ITEMS
        main
        gui
        3d
        2d
        animation
        audio
        resources
        debugger
        theme
)
    # 若为 3d 子目录且开关禁用，则跳过
    if(_subdir STREQUAL "3d")
        if(GODOT_DISABLE_3D)
            continue()
        endif()
    endif()

    # 仅当子目录存在时才加入（避免无文件时报错）
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_subdir}")
        # 约定：各子目录中的 CMakeLists.txt 会调用：
        #   target_sources(scene PRIVATE <该子目录的 *.cpp>)
        add_subdirectory("${_subdir}")
    endif()
endforeach()

# ------------------------------------------------------------------------------
# 4) 向父作用域导出内部库列表，模拟 SCons 的 `env.Prepend(LIBS=[lib])`
#    使用 GODOT_INTERNAL_LIBS 列表变量来“收集”需要在更高层统一链接的内部目标。
#    上层 CMake 可以统一：target_link_libraries(<顶层目标> PRIVATE ${GODOT_INTERNAL_LIBS})
# ------------------------------------------------------------------------------
set(GODOT_INTERNAL_LIBS scene ${GODOT_INTERNAL_LIBS})
set(GODOT_INTERNAL_LIBS "${GODOT_INTERNAL_LIBS}" PARENT_SCOPE)

# 说明：
# - 之所以使用 add_library(scene) 而不是直接将源追加到某个顶层目标，是为了与原构建逻辑一致（先产出一个库，再由上层去链接）。
# - 子目录如需向 scene 追加源码，请在子目录的 CMakeLists.txt 中使用：
#     file(GLOB CONFIGURE_DEPENDS LOCAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
#     target_sources(scene PRIVATE ${LOCAL_SOURCES})
# ------------------------------------------------------------------------------