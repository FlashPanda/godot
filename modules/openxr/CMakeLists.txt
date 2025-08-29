# CMakeLists.txt —— OpenXR 模块（子目录版），等价实现 SCons/SCsub 的编译逻辑
#
# 使用说明：
# - 需在上层 CMakeLists.txt 中已创建目标 godot（add_executable 或 add_library），
#   然后通过 add_subdirectory(本目录) 引入本模块。
# - 本文件实现：
#   1) 根据平台追加 OpenXR 相关宏定义（XR_OS_* / XR_USE_PLATFORM_* 等）。
#   2) 选择性构建 thirdparty/openxr：包含 jsoncpp 与 Khronos loader（Android 除外）。
#   3) 收集本目录 *.cpp 并加入主目标。
#   4) 递归进入 action_map、extensions、scene、editor（可选）子目录。
#   5) 保证第三方变更会触发主目标重建（依赖关系保持）。

cmake_minimum_required(VERSION 3.16)

# 依赖：上层应已定义目标 godot
if(NOT TARGET godot)
    message(FATAL_ERROR "未找到目标 'godot'。请在创建 'godot' 目标后再 add_subdirectory 当前目录。")
endif()

# ========== 选项 ==========
# 是否使用内置 thirdparty/openxr（与 SCons 的 env['builtin_openxr'] 对齐）
option(GODOT_BUILTIN_OPENXR "Build bundled OpenXR (thirdparty/openxr)" ON)

# 是否禁用异常（与 SCons 的 env['disable_exceptions'] 对齐），影响 XRLOADER / jsoncpp 行为
option(GODOT_DISABLE_EXCEPTIONS "Disable exceptions for OpenXR thirdparty sources" OFF)

# Linux 平台下是否启用 X11 / Wayland（与 SCons 的 env['x11'] / env['wayland'] 近似）
option(GODOT_OPENXR_X11     "Define XR_USE_PLATFORM_XLIB on Linux"   ON)
option(GODOT_OPENXR_WAYLAND "Define XR_USE_PLATFORM_EGL on Linux"    OFF)

# 是否为编辑器构建（与 SCons 的 env.editor_build 对齐，用于启用 editor 子目录）
option(GODOT_EDITOR_BUILD "Enable editor build (add editor sources)" OFF)

# ========== 平台相关的公共宏（对应 env_openxr.AppendUnique(CPPDEFINES=...)） ==========
# 说明：此处直接作用到主目标 godot，与 SCons 的公共宏注入一致
if(ANDROID)
    target_compile_definitions(godot PRIVATE XR_OS_ANDROID XR_USE_PLATFORM_ANDROID)
    # 与 SCons 行为一致：Android 下强制禁用 jsoncpp 的异常
    target_compile_definitions(godot PRIVATE JSON_USE_EXCEPTION=0)
elseif(WIN32)
    target_compile_definitions(godot PRIVATE XR_OS_WINDOWS NOMINMAX XR_USE_PLATFORM_WIN32)
elseif(APPLE)
    # Apple 平台没有 XR_USE_PLATFORM_XYZ 的宏
    target_compile_definitions(godot PRIVATE XR_OS_APPLE)
elseif(UNIX)
    # 通常为 Linux（含 FreeBSD 也会命中 UNIX，下面再特判）
    target_compile_definitions(godot PRIVATE XR_OS_LINUX)

    # X11 / Wayland 可选开关
    if(GODOT_OPENXR_X11)
        target_compile_definitions(godot PRIVATE XR_USE_PLATFORM_XLIB)
    endif()

    if(GODOT_OPENXR_WAYLAND)
        # 与 SCons 一致：Wayland 使用 XR_USE_PLATFORM_EGL
        target_compile_definitions(godot PRIVATE XR_USE_PLATFORM_EGL)
    endif()

    # FreeBSD 不定义 HAVE_SECURE_GETENV，其余 Linux 平台定义
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
        target_compile_definitions(godot PRIVATE HAVE_SECURE_GETENV)
    endif()
endif()

# 如需：可在此处定义 XR_USE_TIMESPEC
# target_compile_definitions(godot PRIVATE XR_USE_TIMESPEC)

# ========== 构建 thirdparty/openxr（可选） ==========
if(GODOT_BUILTIN_OPENXR)
    set(OPENXR_3P_DIR "${CMAKE_SOURCE_DIR}/thirdparty/openxr")

    # 头文件搜索路径（与 SCons Prepend 的路径一致）
    # 同时暴露给 thirdparty 对象库与主目标
    set(OPENXR_PUBLIC_INC
            "${OPENXR_3P_DIR}"
            "${OPENXR_3P_DIR}/include"
            "${OPENXR_3P_DIR}/src"
            "${OPENXR_3P_DIR}/src/common"
            "${OPENXR_3P_DIR}/src/external/jsoncpp/include"
    )

    # 仅 thirdparty 使用的对象库，用于隔离编译开关（如禁用警告）
    add_library(openxr_thirdparty OBJECT)

    target_include_directories(openxr_thirdparty
            PUBLIC
            ${OPENXR_PUBLIC_INC}
    )

    # 禁用 thirdparty 的编译警告，等价于 env_thirdparty.disable_warnings()
    target_compile_options(openxr_thirdparty
            PRIVATE
            $<$<C_COMPILER_ID:MSVC>:/w>
            $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:-w>
    )

    # 公共宏：禁用 std::filesystem（与 SCons 对应）
    target_compile_definitions(openxr_thirdparty PRIVATE DISABLE_STD_FILESYSTEM)

    # 异常开关：与 SCons 的 XRLOADER_DISABLE_EXCEPTION_HANDLING / JSON_USE_EXCEPTION=0 对齐
    if(GODOT_DISABLE_EXCEPTIONS)
        target_compile_definitions(openxr_thirdparty PRIVATE
                XRLOADER_DISABLE_EXCEPTION_HANDLING
                JSON_USE_EXCEPTION=0
        )
    endif()

    # jsoncpp 源文件（总是需要）
    target_sources(openxr_thirdparty PRIVATE
            "${OPENXR_3P_DIR}/src/external/jsoncpp/src/lib_json/json_reader.cpp"
            "${OPENXR_3P_DIR}/src/external/jsoncpp/src/lib_json/json_value.cpp"
            "${OPENXR_3P_DIR}/src/external/jsoncpp/src/lib_json/json_writer.cpp"
    )

    # Khronos OpenXR loader 源文件（Android 例外：由设备插件提供，不在引擎内置）
    if(NOT ANDROID)
        # 需要 loader 相关的额外包含路径
        target_include_directories(openxr_thirdparty PRIVATE "${OPENXR_3P_DIR}/src/loader")

        target_sources(openxr_thirdparty PRIVATE
                "${OPENXR_3P_DIR}/src/xr_generated_dispatch_table_core.c"

                "${OPENXR_3P_DIR}/src/common/filesystem_utils.cpp"
                "${OPENXR_3P_DIR}/src/common/object_info.cpp"

                "${OPENXR_3P_DIR}/src/loader/api_layer_interface.cpp"
                "${OPENXR_3P_DIR}/src/loader/loader_core.cpp"
                "${OPENXR_3P_DIR}/src/loader/loader_instance.cpp"
                "${OPENXR_3P_DIR}/src/loader/loader_logger_recorders.cpp"
                "${OPENXR_3P_DIR}/src/loader/loader_logger.cpp"
                "${OPENXR_3P_DIR}/src/loader/manifest_file.cpp"
                "${OPENXR_3P_DIR}/src/loader/runtime_interface.cpp"
                "${OPENXR_3P_DIR}/src/loader/xr_generated_loader.cpp"
        )
    endif()

    # 将 thirdparty 对象并入 godot，并声明依赖关系（保证第三方变更触发重建）
    target_sources(godot PRIVATE $<TARGET_OBJECTS:openxr_thirdparty>)
    add_dependencies(godot openxr_thirdparty)

    # 为主目标也添加头文件搜索路径，方便其它模块（如 scene/）直接包含
    target_include_directories(godot PRIVATE ${OPENXR_PUBLIC_INC})
endif()

# ========== 模块自身源码（等价于 env_openxr.add_source_files(module_obj, "*.cpp")） ==========
file(GLOB OPENXR_MODULE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
if(OPENXR_MODULE_SOURCES)
    target_sources(godot PRIVATE ${OPENXR_MODULE_SOURCES})
endif()

# ========== 子目录（等价于 SConscript("...")） ==========
# 这些子目录应各自提供 CMakeLists.txt；如不存在则跳过
foreach(_subdir IN ITEMS action_map extensions scene)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_subdir}/CMakeLists.txt")
        add_subdirectory("${_subdir}")
    endif()
endforeach()

# 仅编辑器构建时引入 editor 子目录
if(GODOT_EDITOR_BUILD)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/editor/CMakeLists.txt")
        add_subdirectory(editor)
    endif()
endif()

# 提示：如需额外平台宏或特定源文件，可在此处继续追加
# 例如：
# if(UNIX AND NOT APPLE)
#   target_compile_definitions(godot PRIVATE SOME_LINUX_ONLY_MACRO)
# endif()