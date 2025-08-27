# 子级 CMakeLists：不声明 project()，仅定义本目录目标与规则

# 是否使用 ANGLE（若为 ON，则不编译 egl.c）
option(USE_ANGLE "Use ANGLE libraries; skip building glad's egl.c" OFF)

# 仅在 macOS/Windows/Linux(BSD) 下启用该子目标
if(APPLE OR WIN32 OR (UNIX AND NOT APPLE))
    # 第三方 glad 目录
    set(GLAD_DIR "${CMAKE_SOURCE_DIR}/thirdparty/glad")

    # 第三方源文件列表（始终包含 gl.c）
    set(THIRDPARTY_GLAD_SOURCES
            "${GLAD_DIR}/gl.c"
    )
    if(NOT USE_ANGLE)
        list(APPEND THIRDPARTY_GLAD_SOURCES "${GLAD_DIR}/egl.c")
    endif()

    # 当前目录下所有 *.cpp
    file(GLOB LOCAL_CPP_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    )

    # 生成静态库（如需并入更大目标可改为 OBJECT）
    add_library(glad_driver STATIC
            ${LOCAL_CPP_SOURCES}
            ${THIRDPARTY_GLAD_SOURCES}
    )

    # 将 glad 头标记为 system（非 MSVC）；MSVC 下用普通 include
    if (MSVC)
        target_include_directories(glad_driver PRIVATE "${GLAD_DIR}")
    else()
        target_include_directories(glad_driver SYSTEM PRIVATE "${GLAD_DIR}")
    endif()

    # 等价 SCons 的宏定义
    target_compile_definitions(glad_driver PRIVATE
            GLAD_ENABLED
            EGL_ENABLED
    )

    # 为第三方 glad 源禁用告警（仅限这些 .c）
    # MSVC 使用 /W0，GCC/Clang 使用 -w
    foreach(src IN LISTS THIRDPARTY_GLAD_SOURCES)
        if (MSVC)
            set_source_files_properties("${src}" PROPERTIES COMPILE_OPTIONS "/W0")
        else()
            set_source_files_properties("${src}" PROPERTIES COMPILE_OPTIONS "-w")
        endif()
    endforeach()

    # 你可以在上层这样链接：
    # target_link_libraries(upper_target PRIVATE glad_driver)
endif()