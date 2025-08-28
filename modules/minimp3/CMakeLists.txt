# CMakeLists.txt（子目录）
# 作用：
# - 等价替代同目录下的 SCsub 构建行为。
# - 收集本模块 *.cpp 源码，构建为 OBJECT 库供父工程聚合。
# - 根据编译器设置 thirdparty/minimp3 头为系统头（非 MSVC），并提供编译开关以裁剪格式。

cmake_minimum_required(VERSION 3.20)

# 选项：是否启用 minimp3 额外格式（等价于 env["minimp3_extra_formats"]）
# 关闭时会定义 MINIMP3_ONLY_MP3 以仅编译 MP3 支持。
option(MINIMP3_EXTRA_FORMATS "Enable extra formats in minimp3 (unset => MINIMP3_ONLY_MP3)" OFF)

# thirdparty/minimp3 路径位于工程根目录（与 SCons 中 #thirdparty/minimp3/ 的含义相同）
set(MINIMP3_3P_DIR "${PROJECT_SOURCE_DIR}/thirdparty/minimp3")

# 收集本模块源码（等价于 add_source_files(env.modules_sources, "*.cpp")）
file(GLOB MODULE_MINIMP3_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 构建为 OBJECT 库，便于父级聚合
add_library(module_minimp3_obj OBJECT ${MODULE_MINIMP3_SOURCES})

# 本模块自身头文件
target_include_directories(module_minimp3_obj
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

# 第三方头文件：非 MSVC 下作为系统头（避免告警），MSVC 下普通包含
if(MSVC)
    target_include_directories(module_minimp3_obj
            PUBLIC
            "${MINIMP3_3P_DIR}"
    )
else()
    # SYSTEM 会把该目录视为系统头，抑制其内部产生的告警（等价于 SCons 的 -isystem）
    target_include_directories(module_minimp3_obj
            SYSTEM PUBLIC
            "${MINIMP3_3P_DIR}"
    )
endif()

# 若未启用额外格式，则定义仅 MP3 的宏（等价于 env_minimp3.Append(CPPDEFINES=["MINIMP3_ONLY_MP3"])）
if(NOT MINIMP3_EXTRA_FORMATS)
    target_compile_definitions(module_minimp3_obj PUBLIC MINIMP3_ONLY_MP3)
endif()

# 将对象文件导出到父作用域的聚合变量，供父级统一链接
# 父级可将 GODOT_MODULE_OBJECTS 统一添加到最终静态库/可执行文件中
set(GODOT_MODULE_OBJECTS
        ${GODOT_MODULE_OBJECTS}
        $<TARGET_OBJECTS:module_minimp3_obj>
        PARENT_SCOPE
)