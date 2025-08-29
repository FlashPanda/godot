# 子目录 CMakeLists.txt —— 等价实现 iOS SCsub 的核心功能
# 1) 收集并编译本目录 iOS 源文件，加入父级目标 godot
# 2) 可选：生成带 xcframework 布局的 Xcode 工程包并 zip（近似 SCons 的 generate_bundle）

cmake_minimum_required(VERSION 3.20)

# 确保父级已定义目标 godot（通常为 add_library(godot ...) 或 add_executable(godot ...)）
if(NOT TARGET godot)
    message(FATAL_ERROR "未找到父级目标 'godot'。请在 add_subdirectory(本目录) 之前先定义该目标。")
endif()

# -------------------------
# 可配置项（对标 SCons 环境变量）
# -------------------------
# 是否为 dev 构建（影响打包产物命名）
option(IOS_DEV_BUILD "Dev build，用于在包名/库名追加 .dev" OFF)

# 浮点精度（single|double），double 时在包名/库名追加 .double
set(IOS_PRECISION "single" CACHE STRING "Floating precision: single or double")
set_property(CACHE IOS_PRECISION PROPERTY STRINGS single double)

# 额外后缀（对标 env.extra_suffix），例如：".ios-template"、自定义渠道后缀等
set(IOS_EXTRA_SUFFIX "" CACHE STRING "额外库名后缀（如：.custom）")

# 是否生成 Xcode 包（zip）
option(IOS_GENERATE_BUNDLE "生成包含 xcframework 的 Xcode 包并打 zip" OFF)

# 自定义二进制输出目录（缺省为 <project>/bin）
set(IOS_BIN_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH "二进制输出目录（默认 <project>/bin）")

# xcframework 模板所在目录（缺省为 Godot 源中的 misc/dist/ios_xcode）
set(IOS_XCODE_TEMPLATE_DIR "${CMAKE_SOURCE_DIR}/misc/dist/ios_xcode" CACHE PATH "Xcode 模板目录")

# 可选：MoltenVK.xcframework 根目录（若提供，将被复制进包中）
set(IOS_MVK_XCFRAMEWORK_DIR "" CACHE PATH "MoltenVK.xcframework 的目录（可选）")

# -------------------------
# 1) 源文件加入父级目标
# -------------------------
# 等价于 SCsub 中的 ios_lib 列表
set(IOS_PLATFORM_SOURCES
        godot_ios.mm
        os_ios.mm
        main.m
        app_delegate.mm
        view_controller.mm
        ios.mm
        rendering_context_driver_vulkan_ios.mm
        display_server_ios.mm
        joypad_ios.mm
        godot_view.mm
        tts_ios.mm
        display_layer.mm
        godot_app_delegate.m
        godot_view_renderer.mm
        device_metrics.m
        keyboard_input_view.mm
        key_mapping_ios.mm
        ios_terminal_logger.mm
)

# 将本模块源码并入父级目标（相当于 add_library("ios", ...) 再被上游合并）
target_sources(godot PRIVATE ${IOS_PLATFORM_SOURCES})

# (iOS) 启用模块支持（等价于 SCsub: env_ios.Append(CCFLAGS=["-fmodules", "-fcxx-modules"]）
# 仅在使用 Clang/AppleClang 时追加
target_compile_options(godot PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang>:-fmodules -fcxx-modules>
        $<$<CXX_COMPILER_ID:Clang>:-fmodules -fcxx-modules>
)

# -------------------------
# 2) 可选：生成 Xcode 包（zip）
# -------------------------
if(IOS_GENERATE_BUNDLE)
    # 构造命名前缀，等价于 SCsub 中：
    # app_prefix = "godot." + platform [+ .dev][+ .double]
    # rel_prefix = "libgodot." + platform + ".template_release" [+ .dev][+ .double]
    # dbg_prefix = "libgodot." + platform + ".template_debug"   [+ .dev][+ .double]
    set(_platform "ios")
    set(_app_prefix "godot.${_platform}")
    set(_rel_prefix "libgodot.${_platform}.template_release")
    set(_dbg_prefix "libgodot.${_platform}.template_debug")

    if(IOS_DEV_BUILD)
        string(APPEND _app_prefix ".dev")
        string(APPEND _rel_prefix ".dev")
        string(APPEND _dbg_prefix ".dev")
    endif()

    if(IOS_PRECISION STREQUAL "double")
        string(APPEND _app_prefix ".double")
        string(APPEND _rel_prefix ".double")
        string(APPEND _dbg_prefix ".double")
    endif()

    # 对标 SCsub 中的 lipo(...) 输出路径约定：
    # release:   <bin>/<rel_prefix><IOS_EXTRA_SUFFIX>.a
    # debug:     <bin>/<dbg_prefix><IOS_EXTRA_SUFFIX>.a
    # simulator: <bin>/<rel_prefix>.simulator<IOS_EXTRA_SUFFIX>.a / <bin>/<dbg_prefix>.simulator<IOS_EXTRA_SUFFIX>.a
    set(_rel_arm64_lib      "${IOS_BIN_DIR}/${_rel_prefix}${IOS_EXTRA_SUFFIX}.a")
    set(_dbg_arm64_lib      "${IOS_BIN_DIR}/${_dbg_prefix}${IOS_EXTRA_SUFFIX}.a")
    set(_rel_sim_lib        "${IOS_BIN_DIR}/${_rel_prefix}.simulator${IOS_EXTRA_SUFFIX}.a")
    set(_dbg_sim_lib        "${IOS_BIN_DIR}/${_dbg_prefix}.simulator${IOS_EXTRA_SUFFIX}.a")

    # Xcode 包装临时目录与最终 zip 名（将 '.' 替换为 '_'，对标 SCsub）
    set(_app_dir            "${IOS_BIN_DIR}/ios_xcode")
    set(_zip_basename_tmp   "${_app_prefix}${IOS_EXTRA_SUFFIX}")
    string(REPLACE "." "_" _zip_basename "${_zip_basename_tmp}")
    set(_zip_out            "${IOS_BIN_DIR}/${_zip_basename}.zip")

    add_custom_target(ios_generate_xcode_bundle ALL
            # 1) 清理并复制模板目录
            COMMAND ${CMAKE_COMMAND} -E