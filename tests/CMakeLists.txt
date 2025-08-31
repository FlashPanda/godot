# 子目录构建脚本（不要在此处设置 cmake_minimum_required()/project()）
# 目标：等价复刻 SCsub 中的功能
# - 收集当前目录 *.cpp 作为测试源码
# - 生成静态库 tests
# - Windows 下为 doctest 禁用 THREAD_LOCAL
# - 当禁用异常时，为 doctest 启用无异常但保留断言的配置
# - 将 tests 作为库，链接到上层已有目标（默认目标名：godot）

# ========= 可配置项 =========
# 上层已创建的目标（add_executable/add_library 创建），会把 tests 链接进去
set(GODOT_TARGET "godot" CACHE STRING "上层已创建的构建目标名称")

# 是否禁用异常（对应 SCsub 里的 env[\"disable_exceptions\"]）
option(DISABLE_EXCEPTIONS "禁用异常：为 doctest 定义 DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS" OFF)

# ========= 源码收集 =========
# 收集当前目录的所有 .cpp，作为测试源码
# 使用 CONFIGURE_DEPENDS 以便新增/删除源文件时自动触发 CMake 重新配置
file(GLOB TESTS_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 若不存在任何测试源，创建 INTERFACE 库占位，保持链接关系一致
if(TESTS_SOURCES)
    add_library(tests STATIC ${TESTS_SOURCES})
else()
    add_library(tests INTERFACE)
endif()

# ========= 编译定义（等价 SCons 的 CPPDEFINES）=========
# Windows 下禁用 doctest 的线程局部变量，以避免调试器导致的崩溃
# 等价于：DOCTEST_THREAD_LOCAL 设为空定义
if(WIN32)
    target_compile_definitions(tests
            PRIVATE
            DOCTEST_THREAD_LOCAL=
    )
endif()

# 当禁用异常时，为 doctest 打开“无异常但保留全部断言”的配置
if(DISABLE_EXCEPTIONS)
    # 等价于 SCsub 的 DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    target_compile_definitions(tests
            PRIVATE
            DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    )
endif()

# ========= 与上层目标联动 =========
# 将 tests 库“预先”链接到上层已有目标，模拟 SCons 中 env.Prepend(LIBS=[lib]) 的效果
if(TARGET ${GODOT_TARGET})
    target_link_libraries(${GODOT_TARGET} PRIVATE tests)
else()
    message(WARNING
            "未找到上层目标 ${GODOT_TARGET}，已创建 tests 但未自动链接。"
            " 可通过 -DGODOT_TARGET=<target> 指定正确的目标名。")
endif()