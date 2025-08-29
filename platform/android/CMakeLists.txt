# CMakeLists.txt — 子目录版，等价实现 SCsub 的 Android 构建流程

# --------------- 配置项（可在上层通过 -Dxxx=... 传入） ---------------
# Android ABI（决定输出子目录及 NDK libc++_shared 路径）
set(ANDROID_ARCH "" CACHE STRING "Android arch: one of {arm32, arm64, x86_32, x86_64}")

# 构建模式标志（对应 SCsub 中的 env 变量）
option(DEV_BUILD       "Dev build (优先级最高，对应 lib_type_dir=dev)" OFF)
option(DEBUG_FEATURES  "Debug features enabled (决定 debug/release)"  ON)
option(EDITOR_BUILD    "Is editor build (决定是否 tools/ 子目录)"      OFF)
option(STORE_RELEASE   "Store release when editor build"               OFF)

# 生成 APK 及符号配置
option(GENERATE_APK    "Run Gradle to generate APK after build"        OFF)
option(DEBUG_SYMBOLS   "Keep debug symbols (传给 Gradle doNotStrip)"   OFF)

# 指定生成编辑器/模板 APK（对应 SCons 的 env['target'] == 'editor'）
option(TARGET_EDITOR   "Generate editor APK (否则生成 templates APK)"  OFF)

# ANDROID_NDK_ROOT（用于复制 libc++_shared.so）
set(ANDROID_NDK_ROOT "" CACHE PATH "Android NDK root path")

# --------------- 源文件列表（与 SCsub 中 android_files 对应） ---------------
set(ANDROID_SOURCES
        os_android.cpp
        android_input_handler.cpp
        file_access_android.cpp
        file_access_filesystem_jandroid.cpp
        audio_driver_opensl.cpp
        dir_access_jandroid.cpp
        tts_android.cpp
        thread_jandroid.cpp
        net_socket_android.cpp
        java_godot_lib_jni.cpp
        java_class_wrapper.cpp
        java_godot_wrapper.cpp
        java_godot_view_wrapper.cpp
        java_godot_io_wrapper.cpp
        jni_utils.cpp
        android_keys_utils.cpp
        display_server_android.cpp
        plugin/godot_plugin_jni.cpp
        rendering_context_driver_vulkan_android.cpp
        variant/callable_jni.cpp
)

# --------------- thirdparty 对象库（关闭告警后并入主库） ---------------
# 等价于：
#   env_thirdparty = env_android.Clone(); env_thirdparty.disable_warnings()
#   thirdparty_obj = env_thirdparty.SharedObject("#thirdparty/misc/ifaddrs-android.cc")
set(THIRDPARTY_IFADDRS_SRC "${CMAKE_SOURCE_DIR}/thirdparty/misc/ifaddrs-android.cc")

add_library(ifaddrs_android_obj OBJECT "${THIRDPARTY_IFADDRS_SRC}")

# 关闭该 thirdparty 的编译告警
if(MSVC)
    target_compile_options(ifaddrs_android_obj PRIVATE /W0)
else()
    target_compile_options(ifaddrs_android_obj PRIVATE -w)
endif()

# --------------- 主共享库（等价于 add_shared_library("#bin/libgodot", ... )） ---------------
# 注意：
# - 这里目标名为 godot_android，输出名设置为 "godot"，从而生成 libgodot.so（与 SCons 对应）
# - 使用对象库并入 thirdparty（等价于 env.modules_sources += [thirdparty_obj]）
add_library(godot_android SHARED
        ${ANDROID_SOURCES}
        $<TARGET_OBJECTS:ifaddrs_android_obj>
)

# 让最终文件名为 libgodot.so（而不是 libgodot_android.so）
set_target_properties(godot_android PROPERTIES
        OUTPUT_NAME godot
)

# --------------- 架构与输出目录映射（等价于 SCsub 中的逻辑） ---------------
# ABI 目录
set(LIB_ARCH_DIR "")
if(ANDROID_ARCH STREQUAL "arm32")
    set(LIB_ARCH_DIR "armeabi-v7a")
elseif(ANDROID_ARCH STREQUAL "arm64")
    set(LIB_ARCH_DIR "arm64-v8a")
elseif(ANDROID_ARCH STREQUAL "x86_32")
    set(LIB_ARCH_DIR "x86")
elseif(ANDROID_ARCH STREQUAL "x86_64")
    set(LIB_ARCH_DIR "x86_64")
endif()

# 构建类型目录 dev/debug/release
set(LIB_TYPE_DIR "")
if(DEV_BUILD)
    set(LIB_TYPE_DIR "dev")
elseif(DEBUG_FEATURES)
    if(EDITOR_BUILD AND STORE_RELEASE)
        set(LIB_TYPE_DIR "release")
    else()
        set(LIB_TYPE_DIR "debug")
    endif()
else()
    set(LIB_TYPE_DIR "release")
endif()

# tools 子目录
set(LIB_TOOLS_DIR "")
if(EDITOR_BUILD)
    set(LIB_TOOLS_DIR "tools/")
endif()

# 输出目录（与 SCons 一致）
# platform/android/java/lib/libs/[tools/]<debug|release|dev>/<abi>
set(ANDROID_OUT_DIR
        "${CMAKE_SOURCE_DIR}/platform/android/java/lib/libs/${LIB_TOOLS_DIR}${LIB_TYPE_DIR}/${LIB_ARCH_DIR}"
)

# --------------- 复制构建产物与 NDK libc++_shared.so（等价于 Command + Move/Copy） ---------------
# 当 ABI 与类型目录都有效时执行复制
if(LIB_ARCH_DIR AND LIB_TYPE_DIR)
    # NDK libc++_shared.so 路径（按 SCsub 逻辑）
    set(NDK_CXX_SHARED
            "${ANDROID_NDK_ROOT}/sources/cxx-stl/llvm-libc++/libs/${LIB_ARCH_DIR}/libc++_shared.so"
    )

    add_custom_command(TARGET godot_android POST_BUILD
            BYPRODUCTS
            "${ANDROID_OUT_DIR}/libgodot_android.so"
            "${ANDROID_OUT_DIR}/libc++_shared.so"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${ANDROID_OUT_DIR}"
            # 将生成的 libgodot.so 复制并重命名为 libgodot_android.so（SCons 中是 Move）
            COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:godot_android>"
            "${ANDROID_OUT_DIR}/libgodot_android.$<IF:$<BOOL:$<TARGET_FILE_SUFFIX:godot_android>>,so,so>"
            # 复制 NDK 的 libc++_shared.so
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${NDK_CXX_SHARED}"
            "${ANDROID_OUT_DIR}/libc++_shared.so"
            COMMENT "Copying libgodot_android.so and libc++_shared.so to ${ANDROID_OUT_DIR}"
            VERBATIM
    )
else()
    message(WARNING
            "未设置或不支持的 ANDROID_ARCH/构建类型，跳过复制到 APK 目录步骤；输出文件仍会按默认构建目录生成。"
    )
endif()

# --------------- 可选：生成 APK（等价于 generate_apk 自定义命令） ---------------
if(GENERATE_APK)
    # 选择 Gradle 启动脚本（兼容 Windows 与非 Windows）
    if(WIN32)
        set(GRADLEW "gradlew.bat")
        set(GRADLE_CMD "cmd" "/c" "${GRADLEW}")
    else()
        set(GRADLEW "./gradlew")
        set(GRADLE_CMD "${GRADLEW}")
    endif()

    # 任务选择：编辑器或模板
    if(TARGET_EDITOR)
        set(GRADLE_TASK "generateGodotEditor")
    else()
        set(GRADLE_TASK "generateGodotTemplates")
    endif()

    # 额外参数：是否保留符号
    set(GRADLE_ARGS "--quiet")
    if(DEBUG_SYMBOLS)
        list(APPEND GRADLE_ARGS "-PdoNotStrip=true")
    endif()

    add_custom_target(generate_apk ALL
            COMMAND ${GRADLE_CMD} ${GRADLE_TASK} ${GRADLE_ARGS}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/platform/android/java"
            COMMENT "Running Gradle: ${GRADLE_TASK} in platform/android/java"
            VERBATIM
            DEPENDS godot_android
    )
endif()