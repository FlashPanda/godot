# 子目录 CMakeLists：构建 libbacktrace 第三方源码 + 本目录驱动源码（*.cpp）

# 允许上层覆盖第三方源码目录（默认指向工程根下的 thirdparty/libbacktrace）
set(BACKTRACE_THIRDPARTY_DIR
        "${CMAKE_SOURCE_DIR}/thirdparty/libbacktrace"
        CACHE PATH "Path to thirdparty libbacktrace sources"
)

# 1) thirdparty/libbacktrace 源文件列表
set(_libbacktrace_sources
        "${BACKTRACE_THIRDPARTY_DIR}/atomic.c"
        "${BACKTRACE_THIRDPARTY_DIR}/dwarf.c"
        "${BACKTRACE_THIRDPARTY_DIR}/fileline.c"
        "${BACKTRACE_THIRDPARTY_DIR}/posix.c"
        "${BACKTRACE_THIRDPARTY_DIR}/print.c"
        "${BACKTRACE_THIRDPARTY_DIR}/sort.c"
        "${BACKTRACE_THIRDPARTY_DIR}/state.c"
        "${BACKTRACE_THIRDPARTY_DIR}/backtrace.c"
        "${BACKTRACE_THIRDPARTY_DIR}/simple.c"
        "${BACKTRACE_THIRDPARTY_DIR}/pecoff.c"
        "${BACKTRACE_THIRDPARTY_DIR}/read.c"
        "${BACKTRACE_THIRDPARTY_DIR}/alloc.c"
)

# 以 OBJECT 库形式构建第三方对象，便于与其他模块聚合
add_library(thirdparty_backtrace OBJECT ${_libbacktrace_sources})

# 第三方包含目录（SYSTEM 抑制对第三方头的告警）
target_include_directories(thirdparty_backtrace
        SYSTEM
        PRIVATE
        "${BACKTRACE_THIRDPARTY_DIR}"
)

# 关闭第三方代码告警
if(MSVC)
    target_compile_options(thirdparty_backtrace PRIVATE /W0)
else()
    target_compile_options(thirdparty_backtrace PRIVATE -w)
endif()

# 2) 驱动源码（当前目录所有 *.cpp）
file(GLOB _driver_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(backtrace_driver OBJECT ${_driver_sources})

# 当 thirdparty 对象变化时，强制驱动重建（建立目标依赖关系）
add_dependencies(backtrace_driver thirdparty_backtrace)

# 3) 向父级导出对象列表，便于父级 target_sources 聚合
set(BACKTRACE_DRIVER_OBJECTS
        $<TARGET_OBJECTS:thirdparty_backtrace>
        $<TARGET_OBJECTS:backtrace_driver>
        PARENT_SCOPE
)

# 4) 可选：提供一个可直接链接的静态库目标，方便上层直接链接使用
# 上层可以选择：
#   - 方式A：target_sources(<godot_target> PRIVATE ${BACKTRACE_DRIVER_OBJECTS})
#   - 方式B：target_link_libraries(<godot_target> PRIVATE backtrace_driver_lib)
add_library(backtrace_driver_lib STATIC)
target_sources(backtrace_driver_lib
        PRIVATE
        $<TARGET_OBJECTS:thirdparty_backtrace>
        $<TARGET_OBJECTS:backtrace_driver>
)
# 暴露第三方头给链接该静态库的目标（SYSTEM 可抑制告警）
target_include_directories(backtrace_driver_lib
        SYSTEM PUBLIC
        "${BACKTRACE_THIRDPARTY_DIR}"
)