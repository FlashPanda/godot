# 作为 main/CMakeLists.txt 使用

# 确保上层已有目标 godot
if(NOT TARGET godot)
    message(FATAL_ERROR "目标 'godot' 未定义。请先在上层定义（add_executable(godot ...)），再 add_subdirectory(main)。")
endif()

# 选项与开关，对应 SCsub 的 env["..."] 与 env.editor_build 等
# 可在上层通过 -D 或 set(...) 传入
option(EDITOR_BUILD         "是否为编辑器构建（对应 env.editor_build）" ON)
option(STEAMAPI             "是否启用 Steam API（对应 env[\"steamapi\"]）" OFF)
option(TESTS                "是否启用测试（对应 env[\"tests\"]）" OFF)
option(NO_EDITOR_SPLASH     "是否禁用编辑器启动图生成（对应 env[\"no_editor_splash\"]）" OFF)

# 收集本目录下的 .cpp 源文件
file(GLOB LOCAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Python 解释器用于运行 main_builders 脚本函数
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# 生成目录与包含目录设置
# 生成的头会放在 build/.../main/ 下，这样配合包含路径 ${CMAKE_BINARY_DIR} 可用 "main/xxx.gen.h" 来包含
set(_MAIN_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# --- 生成 splash.gen.h ---
set(SPLASH_PNG "${CMAKE_CURRENT_SOURCE_DIR}/splash.png")
set(SPLASH_GEN "${_MAIN_BIN_DIR}/splash.gen.h")

add_custom_command(
        OUTPUT  "${SPLASH_GEN}"
        COMMAND ${Python3_EXECUTABLE}
        -c
        "import sys; sys.path.insert(0, r'${CMAKE_CURRENT_SOURCE_DIR}');
from main_builders import make_splash as _m; _m([sys.argv[1]],[sys.argv[2]],None)"
        "${SPLASH_GEN}" "${SPLASH_PNG}"
        DEPENDS "${SPLASH_PNG}" "${CMAKE_CURRENT_SOURCE_DIR}/main_builders.py"
        COMMENT "Generating splash.gen.h from splash.png"
        VERBATIM
)

set(_MAIN_GENERATED_HEADERS "${SPLASH_GEN}")

# --- 生成 splash_editor.gen.h（仅在编辑器构建且未禁用 editor splash 时） ---
if(EDITOR_BUILD AND NOT NO_EDITOR_SPLASH)
    set(SPLASH_EDITOR_PNG "${CMAKE_CURRENT_SOURCE_DIR}/splash_editor.png")
    set(SPLASH_EDITOR_GEN "${_MAIN_BIN_DIR}/splash_editor.gen.h")

    add_custom_command(
            OUTPUT  "${SPLASH_EDITOR_GEN}"
            COMMAND ${Python3_EXECUTABLE}
            -c
            "import sys; sys.path.insert(0, r'${CMAKE_CURRENT_SOURCE_DIR}');
from main_builders import make_splash_editor as _m; _m([sys.argv[1]],[sys.argv[2]],None)"
            "${SPLASH_EDITOR_GEN}" "${SPLASH_EDITOR_PNG}"
            DEPENDS "${SPLASH_EDITOR_PNG}" "${CMAKE_CURRENT_SOURCE_DIR}/main_builders.py"
            COMMENT "Generating splash_editor.gen.h from splash_editor.png"
            VERBATIM
    )

    list(APPEND _MAIN_GENERATED_HEADERS "${SPLASH_EDITOR_GEN}")
endif()

# --- 生成 app_icon.gen.h ---
set(APP_ICON_PNG "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.png")
set(APP_ICON_GEN "${_MAIN_BIN_DIR}/app_icon.gen.h")

add_custom_command(
        OUTPUT  "${APP_ICON_GEN}"
        COMMAND ${Python3_EXECUTABLE}
        -c
        "import sys; sys.path.insert(0, r'${CMAKE_CURRENT_SOURCE_DIR}');
from main_builders import make_app_icon as _m; _m([sys.argv[1]],[sys.argv[2]],None)"
        "${APP_ICON_GEN}" "${APP_ICON_PNG}"
        DEPENDS "${APP_ICON_PNG}" "${CMAKE_CURRENT_SOURCE_DIR}/main_builders.py"
        COMMENT "Generating app_icon.gen.h from app_icon.png"
        VERBATIM
)

list(APPEND _MAIN_GENERATED_HEADERS "${APP_ICON_GEN}")

# 聚合生成任务，确保在需要时先生成
add_custom_target(main_assets_gen DEPENDS ${_MAIN_GENERATED_HEADERS})

# 创建静态库 main（对应 SCons 的 add_library("main", ...)），并设置包含目录与编译宏
add_library(main STATIC ${LOCAL_SOURCES} ${_MAIN_GENERATED_HEADERS})
add_dependencies(main main_assets_gen)

# 让源码能通过 #include "main/xxx.gen.h" 找到生成的头
target_include_directories(main PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"  # build/.../main/
        "${CMAKE_BINARY_DIR}"          # 允许以 "main/..." 形式包含
)

# 条件性编译宏
if(STEAMAPI AND EDITOR_BUILD)
    target_compile_definitions(main PRIVATE STEAMAPI_ENABLED)
endif()

if(TESTS)
    target_compile_definitions(main PRIVATE TESTS_ENABLED)
endif()

# 将 main 链接到上层可执行目标 godot（对应 env.Prepend(LIBS=[lib])）
target_link_libraries(godot PRIVATE main)

# 同时给 godot 增加包含目录，以便其它目标源码也能包含这些生成头
target_include_directories(godot PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_BINARY_DIR}"
)