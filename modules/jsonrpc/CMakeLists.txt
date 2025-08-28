# CMake

# 将当前目录下所有 .cpp 加入到父目标 godot
file(GLOB JSONRPC_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
target_sources(godot PRIVATE ${JSONRPC_LOCAL_SOURCES})

# 选项：是否编译本模块的测试（等价于 env["tests"]）
option(JSONRPC_ENABLE_TESTS "Enable tests for jsonrpc module" OFF)

if(JSONRPC_ENABLE_TESTS)
    # 定义 TESTS_ENABLED（等价于 Append CPPDEFINES=["TESTS_ENABLED"]）
    target_compile_definitions(godot PRIVATE TESTS_ENABLED)

    # 追加 tests 目录下的 .cpp
    file(GLOB JSONRPC_TEST_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    )
    target_sources(godot PRIVATE ${JSONRPC_TEST_SOURCES})

    # 选项：在禁用异常时为 doctest 定义特殊宏（等价于 env["disable_exceptions"] 时追加宏）
    option(JSONRPC_DISABLE_EXCEPTIONS "Use doctest no-exceptions config for jsonrpc tests" OFF)
    if(JSONRPC_DISABLE_EXCEPTIONS)
        target_compile_definitions(godot PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
    endif()
endif()