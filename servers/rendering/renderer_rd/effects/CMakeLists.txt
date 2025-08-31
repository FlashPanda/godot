cmake_minimum_required(VERSION 3.16) # 子目录通常由上层统一设置版本，此处仅作保守声明

# 说明：
# - 本 CMakeLists.txt 作为子目录使用，父级应已创建目标 `godot`（可执行或库），并 add_subdirectory() 引入本目录。
# - 功能等价于 SCons 的逻辑：
#   1) 将 thirdparty/amd-fsr2 下的 ffx_assert.cpp、ffx_fsr2.cpp 编译并加入构建，同时为其添加包含目录与关闭告警；
#   2) 为相关代码添加编译定义 FFX_GCC；
#   3) 将当前目录下的所有 *.cpp 文件加入构建；
#   4) 若启用 Metal（原脚本中的 env["metal"]），则额外加入 metal_fx.mm；
#   5) 第三方与模块源都直接并入同一目标，文件级依赖将由 CMake 自动跟踪。

# 如果上层未定义开关，则在此提供一个默认开关（上层可覆盖）
if(NOT DEFINED GODOT_ENABLE_METAL)
    option(GODOT_ENABLE_METAL "Enable Metal-specific sources in this module" OFF)
endif()

# 必须存在上层目标 `godot`
if(NOT TARGET godot)
    message(FATAL_ERROR "未找到构建目标 'godot'。请在父级 CMake 中先创建该目标后，再 add_subdirectory 引入本目录。")
endif()

# 第三方 FSR2 源码所在目录（相对于工程根目录）
# 注：SCons 中的 '#thirdparty/...' 为工程根路径；此处使用 PROJECT_SOURCE_DIR 来对应工程根。
set(FSR2_DIR "${PROJECT_SOURCE_DIR}/thirdparty/amd-fsr2")

# 第三方源文件（显式列出更稳定，避免 GLOB 带来的 CMake 重新配置触发问题）
set(FSR2_SOURCES
        "${FSR2_DIR}/ffx_assert.cpp"
        "${FSR2_DIR}/ffx_fsr2.cpp"
)

# 当前模块源文件（匹配 *.cpp）
file(GLOB MODULE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 可选：当启用 Metal 时，追加 metal_fx.mm（若文件存在则加入）
set(METAL_SOURCES "")
if(GODOT_ENABLE_METAL)
    set(_metal_file "${CMAKE_CURRENT_SOURCE_DIR}/metal_fx.mm")
    if(EXISTS "${_metal_file}")
        list(APPEND METAL_SOURCES "${_metal_file}")
    else()
        message(STATUS "启用 Metal，但未找到 ${_metal_file}，将跳过该文件。")
    endif()
endif()

# 将源文件加入上层目标 `godot`
target_sources(godot PRIVATE
        ${MODULE_SOURCES}
        ${METAL_SOURCES}
        ${FSR2_SOURCES}
)

# 为第三方目录添加包含路径（等价于 Prepend(CPPPATH=[thirdparty_dir])）
target_include_directories(godot PRIVATE
        "${FSR2_DIR}"
)

# 添加编译定义（等价于 Append(CPPDEFINES=["FFX_GCC"]））
# 说明：原注释表示该宏仅用于控制符号导出，在此同样添加到目标
target_compile_definitions(godot PRIVATE
        FFX_GCC
)

# 仅对第三方源码关闭告警（等价于 env_thirdparty.disable_warnings()）
# - MSVC 使用 /W0
# - 其他编译器使用 -w
set_source_files_properties(${FSR2_SOURCES} PROPERTIES
        COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/W0>;$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>"
)

# 可选：在 IDE 中对源文件分组，便于查看
if(MODULE_SOURCES)
    source_group("module" FILES ${MODULE_SOURCES})
endif()
if(METAL_SOURCES)
    source_group("module\\metal" FILES ${METAL_SOURCES})
endif()
if(FSR2_SOURCES)
    source_group("thirdparty\\amd-fsr2" FILES ${FSR2_SOURCES})
endif()

# 关于依赖：
# - 原脚本使用 env.Depends(module_obj, thirdparty_obj) 强制当第三方库更新时重编模块对象。
# - 在本 CMake 方案中，第三方与模块文件都直接并入同一目标 `godot`，
#   CMake 会自动跟踪文件级依赖，源文件更新会触发对应的重新编译，无需额外手工依赖。