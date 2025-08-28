# 子目录 CMakeLists.txt：模块 etcpak

# 1) thirdparty/etcpak 源文件（位于工程根目录）
file(GLOB THIRD_PARTY_ETCPAK_SOURCES CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/thirdparty/etcpak/*.cpp"
)

# 使用 OBJECT 库承载第三方源码，便于与模块源码一起并入 godot 目标
add_library(etcpak_thirdparty_obj OBJECT
        ${THIRD_PARTY_ETCPAK_SOURCES}
)

# 第三方头文件路径
target_include_directories(etcpak_thirdparty_obj PUBLIC
        "${PROJECT_SOURCE_DIR}/thirdparty/etcpak"
)

# 禁用第三方代码的编译告警
if(MSVC)
    target_compile_options(etcpak_thirdparty_obj PRIVATE /W0)
else()
    target_compile_options(etcpak_thirdparty_obj PRIVATE -w)
endif()

# 2) 模块本目录 *.cpp
file(GLOB MODULE_ETCPAK_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(etcpak_module_obj OBJECT
        ${MODULE_ETCPAK_SOURCES}
)

# 模块也需要能包含第三方头
target_include_directories(etcpak_module_obj PRIVATE
        "${PROJECT_SOURCE_DIR}/thirdparty/etcpak"
)

# 当第三方对象发生变化时，强制触发模块对象的重编译
add_dependencies(etcpak_module_obj etcpak_thirdparty_obj)

# 3) 将两部分对象并入上层已定义的可执行目标 godot
if(TARGET godot)
    target_sources(godot PRIVATE
            $<TARGET_OBJECTS:etcpak_thirdparty_obj>
            $<TARGET_OBJECTS:etcpak_module_obj>
    )
else()
    message(FATAL_ERROR "Target 'godot' not found. Please add this subdirectory after defining the 'godot' target.")
endif()