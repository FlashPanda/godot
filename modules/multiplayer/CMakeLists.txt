# 最低版本要求，可按项目统一版本调整
cmake_minimum_required(VERSION 3.20)

# 推导模块名为当前目录名，避免硬编码
get_filename_component(MODULE_NAME "${CMAKE_CURRENT_SOURCE_DIR}" NAME)

# 选项设计：
# - GODOT_EDITOR_BUILD：是否包含 editor/*.cpp（对应 SCons 的 env.editor_build）
# - GODOT_ENABLE_TESTS：是否启用测试并定义 TESTS_ENABLED（对应 env["tests"]）
# - GODOT_DISABLE_EXCEPTIONS：测试时禁用异常时使用的 Doctest 宏（对应 env["disable_exceptions"]）
# 这些选项通常由上层工程统一传入，这里仅在未定义时提供默认值，避免覆盖父级设置。
if(NOT DEFINED GODOT_EDITOR_BUILD)
    set(GODOT_EDITOR_BUILD OFF CACHE BOOL "Enable building editor sources for this module")
endif()
if(NOT DEFINED GODOT_ENABLE_TESTS)
    set(GODOT_ENABLE_TESTS OFF CACHE BOOL "Enable tests for this module")
endif()
if(NOT DEFINED GODOT_DISABLE_EXCEPTIONS)
    set(GODOT_DISABLE_EXCEPTIONS OFF CACHE BOOL "Disable exceptions for tests configuration")
endif()

# 启用 C++17（与工程一致）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1) 收集当前目录下的模块源码（等价于 add_source_files(module_obj, "*.cpp")）
# 使用 CONFIGURE_DEPENDS 以便增删文件时触发重新配置
file(GLOB MODULE_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 2) 若为编辑器构建，加入 editor/*.cpp（等价于 env.editor_build 分支）
if(GODOT_EDITOR_BUILD AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/editor")
    file(GLOB MODULE_EDITOR_SOURCES
            CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp"
    )
    list(APPEND MODULE_SOURCES ${MODULE_EDITOR_SOURCES})
endif()

# 3) 若启用测试，定义 TESTS_ENABLED 并加入 tests/*.cpp
if(GODOT_ENABLE_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_compile_definitions(TESTS_ENABLED)
    file(GLOB MODULE_TEST_SOURCES
            CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    )
    list(APPEND MODULE_SOURCES ${MODULE_TEST_SOURCES})

    # 若禁用异常，则为 Doctest 定义无异常宏（对应 SCons 里的 CPPDEFINES 追加）
    if(GODOT_DISABLE_EXCEPTIONS)
        add_compile_definitions(DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
    endif()
endif()

# 4) 若没有源文件，则不创建目标，直接返回（避免空目标警告）
if(MODULE_SOURCES STREQUAL "")
    message(STATUS "[${MODULE_NAME}] No sources found, skip building this module.")
    # 同步向父作用域回传空列表，保持接口一致
    set(MODULE_${MODULE_NAME}_SOURCES "" PARENT_SCOPE)
    return()
endif()

# 5) 创建静态库目标，供上层链接使用（等价于在 SCons 中聚合到 env.modules_sources 中统一编译/链接）
add_library(${MODULE_NAME} STATIC ${MODULE_SOURCES})

# 6) 设定包含目录（当前目录与可选 editor 目录），供外部可见
target_include_directories(${MODULE_NAME}
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "$<$<BOOL:${GODOT_EDITOR_BUILD}>:${CMAKE_CURRENT_SOURCE_DIR}/editor>"
)

# 7) 将本模块源列表回传给父作用域，便于父级统一聚合（对应 env.modules_sources += module_obj）
# 父级可以选择：
# - 使用 MODULE_<name>_SOURCES 做合并编译；或
# - 直接 target_link_libraries(上层目标 PRIVATE ${MODULE_NAME})
set(MODULE_${MODULE_NAME}_SOURCES "${MODULE_SOURCES}" PARENT_SCOPE)

# 可选：根据需要继承父级的通用编译选项、警告、特性等
# 例如：
# target_compile_options(${MODULE_NAME} PRIVATE ${PROJECT_COMMON_CXX_FLAGS})