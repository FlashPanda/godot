# CMake 子目录脚本：实现与 SCons SCsub 等价的构建逻辑
# - 将当前目录下 *.cpp 并入父级目标（类似 env.add_source_files(module_obj, "*.cpp")）
# - 引入 thirdparty/tinyexr/tinyexr.cc 源，并添加包含目录与编译宏
# - 尽量为 thirdparty 源关闭警告（模拟 SCons 的 disable_warnings）
# - 通过把 thirdparty 源与模块源一起加入同一目标，确保 thirdparty 更新会触发模块重编译

# 1) 选择父级目标名称
#    - 默认父级目标名为 godot（可执行或库）
#    - 若父级使用其它目标名，可在 add_subdirectory 之前设置 GODOT_PARENT_TARGET 覆盖
if (NOT DEFINED GODOT_PARENT_TARGET)
    set(GODOT_PARENT_TARGET godot)
endif()

if (NOT TARGET ${GODOT_PARENT_TARGET})
    message(FATAL_ERROR
            "未找到父级目标 '${GODOT_PARENT_TARGET}'。请先在父级 CMakeLists.txt 中定义该目标，"
            "再 add_subdirectory 进本目录，或通过 set(GODOT_PARENT_TARGET <目标名>) 指定。")
endif()

# 2) 本模块源文件：收集当前目录下的所有 .cpp
file(GLOB MODULE_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 3) thirdparty/tinyexr 配置与源文件
#    - SCons 中使用 #thirdparty/tinyexr/ 作为包含路径并添加 tinyexr.cc
#    - 这里按工程根路径推导 thirdparty 目录
set(TINYEXR_THIRDPARTY_DIR "${PROJECT_SOURCE_DIR}/thirdparty/tinyexr")
set(TINYEXR_SOURCES
        "${TINYEXR_THIRDPARTY_DIR}/tinyexr.cc"
)

# 4) 将包含目录与编译宏加到父级目标（等价于 Prepend(CPPPATH) 与 Append(CPPDEFINES)）
#    - 启用多线程加载：TINYEXR_USE_THREAD
#    - 禁用 miniz，改用已有 zlib：TINYEXR_USE_MINIZ=0
target_include_directories(${GODOT_PARENT_TARGET} PRIVATE
        "${TINYEXR_THIRDPARTY_DIR}"
)
target_compile_definitions(${GODOT_PARENT_TARGET} PRIVATE
        TINYEXR_USE_THREAD
        TINYEXR_USE_MINIZ=0
)

# 5) 将本模块与 thirdparty 源并入父级目标
if (MODULE_LOCAL_SOURCES)
    target_sources(${GODOT_PARENT_TARGET} PRIVATE ${MODULE_LOCAL_SOURCES})
endif()
target_sources(${GODOT_PARENT_TARGET} PRIVATE ${TINYEXR_SOURCES})

# 6) 尽量为 thirdparty 源关闭编译警告（模拟 env_thirdparty.disable_warnings()）
#    - 使用每源文件编译选项，避免影响整个目标的其他源码
if (MSVC)
    set_source_files_properties(${TINYEXR_SOURCES}
            PROPERTIES COMPILE_OPTIONS "/w")   # 关闭所有级别警告
else()
    set_source_files_properties(${TINYEXR_SOURCES}
            PROPERTIES COMPILE_OPTIONS "-w")   # GCC/Clang：关闭所有警告
endif()