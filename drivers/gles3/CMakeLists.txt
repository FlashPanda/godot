# 子级 CMakeLists：不声明 project()，仅组织本目录与子目录源码

# 收集当前目录下的 *.cpp
file(GLOB LOCAL_CPP_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 使用 OBJECT 库聚合源码，便于上层复用对象文件
add_library(renderer_section OBJECT ${LOCAL_CPP_SOURCES})

# 可选地将此目标的包含目录/编译选项放在这里（如有需要）
# target_include_directories(renderer_section PRIVATE ...)
# target_compile_definitions(renderer_section PRIVATE ...)

# 递归引入子目录（存在 CMakeLists.txt 才加入，避免报错）
foreach(_subdir shaders storage effects environment)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_subdir}/CMakeLists.txt")
        add_subdirectory("${_subdir}")
    endif()
endforeach()

# 上层可通过 $<TARGET_OBJECTS:renderer_section> 使用本目标对象文件，
# 或在更高层创建最终库/可执行程序并 target_link_libraries 关联其他子目标。