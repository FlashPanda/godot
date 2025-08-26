# 选项与 SCons 变量对应
option(builtin_mbedtls "Use the built-in mbedTLS library" ON)
option(module_mbedtls_enabled "Whether the mbedTLS module is enabled" OFF)

# 收集当前目录下 C++ 源文件
file(GLOB LOCAL_SOURCES "*.cpp")

# 当为内置或模块未启用时，使用 thirdparty/mbedtls/include 头文件
if(builtin_mbedtls OR NOT module_mbedtls_enabled)
    target_include_directories(godot PRIVATE
            ${CMAKE_SOURCE_DIR}/thirdparty/mbedtls/include
    )
endif()

# 如果未启用 mbedtls 模块，则在此最小化编译 mbedTLS 所需 C 源文件，并设置核心配置头
if(NOT module_mbedtls_enabled)
    set(_MBEDTLS_CONFIG_HEADER
            ${CMAKE_SOURCE_DIR}/thirdparty/mbedtls/include/godot_core_mbedtls_config.h
    )

    # 配置文件路径的宏定义（兼容 Ninja + MSVC 使用 <...>，其余使用 "..."）
    if(MSVC AND CMAKE_GENERATOR STREQUAL "Ninja")
        target_compile_definitions(godot PRIVATE MBEDTLS_CONFIG_FILE=<${_MBEDTLS_CONFIG_HEADER}>)
    else()
        # 需要转义引号
        target_compile_definitions(godot PRIVATE MBEDTLS_CONFIG_FILE=\"${_MBEDTLS_CONFIG_HEADER}\")
    endif()

    # 最小 mbedTLS 源
    set(_MBEDTLS_THIRDPARTY_DIR ${CMAKE_SOURCE_DIR}/thirdparty/mbedtls/library)
    set(_MBEDTLS_MIN_SRCS
            ${_MBEDTLS_THIRDPARTY_DIR}/aes.c
            ${_MBEDTLS_THIRDPARTY_DIR}/base64.c
            ${_MBEDTLS_THIRDPARTY_DIR}/constant_time.c
            ${_MBEDTLS_THIRDPARTY_DIR}/ctr_drbg.c
            ${_MBEDTLS_THIRDPARTY_DIR}/entropy.c
            ${_MBEDTLS_THIRDPARTY_DIR}/md.c
            ${_MBEDTLS_THIRDPARTY_DIR}/md5.c
            ${_MBEDTLS_THIRDPARTY_DIR}/sha1.c
            ${_MBEDTLS_THIRDPARTY_DIR}/sha256.c
            ${_MBEDTLS_THIRDPARTY_DIR}/godot_core_mbedtls_platform.c
    )

    # 关闭这些第三方源码的告警（与 SCons 中 disable_warnings 对齐）
    if(MSVC)
        set_source_files_properties(${_MBEDTLS_MIN_SRCS} PROPERTIES COMPILE_OPTIONS "/w")
    else()
        set_source_files_properties(${_MBEDTLS_MIN_SRCS} PROPERTIES COMPILE_OPTIONS "-w")
    endif()

    # 附加到 godot 目标
    target_sources(godot PRIVATE
            ${_MBEDTLS_MIN_SRCS}
    )

    # 当配置头变化时，触发一次“虚拟文件”重新生成，从而促使目标参与重建
    # 说明：虽然编译器通常能通过依赖扫描发现宏包含的头，但这里显式添加一个受该头依赖的
    # 生成文件，以确保变化被 CMake/Ninja/VS 工具链稳定感知（对应 SCons 的 Depends）。
    set(_MBEDTLS_CONFIG_STAMP ${CMAKE_CURRENT_BINARY_DIR}/mbedtls_config_core.stamp)
    add_custom_command(
            OUTPUT ${_MBEDTLS_CONFIG_STAMP}
            COMMAND ${CMAKE_COMMAND} -E echo "mbedtls core config touched at $<IF:$<BOOL:$<TARGET_EXISTS:godot>>,$<TARGET_FILE:godot>,configure-time>" > ${_MBEDTLS_CONFIG_STAMP}
            DEPENDS ${_MBEDTLS_CONFIG_HEADER}
            VERBATIM
    )
    # 将该“印记”文件加入到目标源中，任何其变化都会引发目标的重新编译/链接路径
    target_sources(godot PRIVATE ${_MBEDTLS_CONFIG_STAMP})

elseif(builtin_mbedtls)
    # 模块启用且选择使用内置时，为核心代码设置 module 配置头的宏定义
    set(_MBEDTLS_CONFIG_HEADER
            ${CMAKE_SOURCE_DIR}/thirdparty/mbedtls/include/godot_module_mbedtls_config.h
    )
    if(MSVC AND CMAKE_GENERATOR STREQUAL "Ninja")
        target_compile_definitions(godot PRIVATE MBEDTLS_CONFIG_FILE=<${_MBEDTLS_CONFIG_HEADER}>)
    else()
        target_compile_definitions(godot PRIVATE MBEDTLS_CONFIG_FILE=\"${_MBEDTLS_CONFIG_HEADER}\")
    endif()

    # 同样添加一个受该配置头依赖的“印记”文件来稳定触发重建
    set(_MBEDTLS_CONFIG_STAMP ${CMAKE_CURRENT_BINARY_DIR}/mbedtls_config_module.stamp)
    add_custom_command(
            OUTPUT ${_MBEDTLS_CONFIG_STAMP}
            COMMAND ${CMAKE_COMMAND} -E echo "mbedtls module config touched at $<IF:$<BOOL:$<TARGET_EXISTS:godot>>,$<TARGET_FILE:godot>,configure-time>" > ${_MBEDTLS_CONFIG_STAMP}
            DEPENDS ${_MBEDTLS_CONFIG_HEADER}
            VERBATIM
    )
    target_sources(godot PRIVATE ${_MBEDTLS_CONFIG_STAMP})
endif()

# 将本目录下 *.cpp 源附加到目标（对应 env_crypto.add_source_files(core_obj, "*.cpp")）
target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
)