# 子目录 CMakeLists.txt：模块 ogg

# 确认上层已定义目标 godot（需在 add_subdirectory(本目录) 之前创建）
if(NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Please add this subdirectory after defining the 'godot' target.")
endif()

# 选项：是否使用内置 thirdparty/libogg（等价于 env['builtin_libogg']）
# 上层也可通过 -DBUILTIN_LIBOGG=ON/OFF 来控制
if(NOT DEFINED BUILTIN_LIBOGG)
    option(BUILTIN_LIBOGG "Use bundled thirdparty/libogg sources" ON)
endif()

# 1) 模块本目录源码（等价于 env_ogg.add_source_files(module_obj, \"*.cpp\"))
file(GLOB OGG_MODULE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 2) 当使用内置 thirdparty/libogg 时，收集并编译 C 源码，关闭其告警，并为模块添加包含目录
set(THIRDPARTY_OGG_DIR "${PROJECT_SOURCE_DIR}/thirdparty/libogg")
set(THIRDPARTY_OGG_SOURCES
        "${THIRDPARTY_OGG_DIR}/bitwise.c"
        "${THIRDPARTY_OGG_DIR}/framing.c"
)

if(BUILTIN_LIBOGG)
    # 第三方对象库（承载 thirdparty 源）；使用 SYSTEM include 并禁用编译警告
    add_library(ogg_thirdparty_obj OBJECT
            ${THIRDPARTY_OGG_SOURCES}
    )
    target_include_directories(ogg_thirdparty_obj SYSTEM PUBLIC
            "${THIRDPARTY_OGG_DIR}"
    )
    # 关闭告警：MSVC /W0；GCC/Clang -w
    if(MSVC)
        target_compile_options(ogg_thirdparty_obj PRIVATE /W0)
    else()
        target_compile_options(ogg_thirdparty_obj PRIVATE -w)
    endif()

    # 模块对象库（承载本目录 *.cpp）
    add_library(ogg_module_obj OBJECT
            ${OGG_MODULE_SOURCES}
    )
    # 等价于 env_ogg.Prepend(CPPPATH=[thirdparty_dir])（使模块可包含 thirdparty 头）
    target_include_directories(ogg_module_obj PRIVATE
            "${THIRDPARTY_OGG_DIR}"
    )

    # 等价于 env.Depends(module_obj, thirdparty_obj)
    add_dependencies(ogg_module_obj ogg_thirdparty_obj)

    # 将对象统一并入上层目标 godot
    target_sources(godot PRIVATE
            $<TARGET_OBJECTS:ogg_thirdparty_obj>
            $<TARGET_OBJECTS:ogg_module_obj>
    )
else()
    # 不使用内置 thirdparty 的情况下，仅并入模块源码；
    # 链接系统 Ogg（若可用），否则保持静默，交由上层统一处理依赖。
    target_sources(godot PRIVATE
            ${OGG_MODULE_SOURCES}
    )
    find_package(Ogg QUIET)
    if(TARGET Ogg::ogg)
        target_link_libraries(godot PRIVATE Ogg::ogg)
    endif()
endif()