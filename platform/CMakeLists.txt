# 子目录 CMakeLists.txt —— 对应 SCsub 的“平台导出图标生成 + 平台独占 API 注册/编译”功能
# 使用说明（上层使用示例）：
#   add_subdirectory(path/to/this_subdir)
#   set(PLATFORM_EXPORTERS "windows;macos;web" CACHE STRING "")
#   set(PLATFORM_APIS "windows;macos;web" CACHE STRING "")
#
# 行为概述：
# 1) 为每个 PLATFORM_EXPORTERS 项目，扫描 <p>/export/*.svg，生成对应 <same_name>_svg.gen.h，
#    头文件内容是 static const char *_{p}_{name}_svg = "..." 的 C 字符串（内容经过转义）。
# 2) 基于 PLATFORM_APIS 生成 register_platform_apis.gen.cpp，展开：
#       #include "<p>/api/api.h"
#       void register_platform_apis()   { register_<p>_api(); ... }
#       void unregister_platform_apis() { unregister_<p>_api(); ... }
#    同时把每个 <p>/api/*.cpp 编进名为 platform 的静态库。
# 3) 上层可使用 target_link_libraries(你的目标 PRIVATE platform) 将其加入链接。
#
# 注意：
# - 这是子目录脚本，不创建可执行程序，仅创建/更新静态库目标 platform。
# - 如果没有传入任何平台列表，也会正确地只生成空的库或跳过对应步骤。
# - 生成文件位于 ${CMAKE_CURRENT_BINARY_DIR}/generated 下。

cmake_minimum_required(VERSION 3.20)

# 允许上层以 -D 传入，或在 add_subdirectory 之前 set(...)。分号分隔。
set(PLATFORM_EXPORTERS "" CACHE STRING "参与导出图标生成的各平台目录名（分号分隔）")
set(PLATFORM_APIS      "" CACHE STRING "参与 API 注册与编译的各平台目录名（分号分隔）")

# 统一输出目录：生成文件放到本子目录的 build 生成路径下
set(_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${_GEN_DIR}")

# 1) 准备一个 CMake 脚本，用于把 SVG 转成 C 头文件中可用的原始字符串常量
# 该脚本在构建阶段由 add_custom_command 调用，确保当 SVG 变化时会重新生成对应头文件。
set(_SVG_2_HDR "${_GEN_DIR}/svg_to_header.cmake")
file(WRITE "${_SVG_2_HDR}" "
# 读取参数：INPUT/OUTPUT/PLATFORM/NAME
if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED PLATFORM OR NOT DEFINED NAME)
  message(FATAL_ERROR \"svg_to_header.cmake 缺少必要参数\")
endif()

file(READ \"\${INPUT}\" _content)

# 依次转义：反斜杠、双引号、回车换行
string(REPLACE \"\\\\\" \"\\\\\\\\\" _content \"\${_content}\")
string(REPLACE \"\\\"\" \"\\\\\\\"\" _content \"\${_content}\")
# 将换行替换为 \\n\" 换行 \" 的形式，减小行长并更接近原脚本输出的效果
string(REPLACE \"\\n\" \"\\\\n\\\"\\n\\\"\" _content \"\${_content}\")

# 写出头文件内容
file(WRITE \"\${OUTPUT}\" \"/* Auto-generated from: \${INPUT} */\\n\")
file(APPEND \"\${OUTPUT}\" \"static const char *_\${PLATFORM}_\${NAME}_svg = \\\"\${_content}\\\";\\n\")
")

# 2) 遍历 PLATFORM_EXPORTERS，收集并为每个 svg 生成 *_svg.gen.h 的构建规则
set(_EXPORT_ICON_HEADERS "")
set(_EXPORT_ICONS_TARGETS "")

foreach(_P IN LISTS PLATFORM_EXPORTERS)
    # 扫描 <platform>/export/*.svg
    file(GLOB _svgs CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/${_P}/export/*.svg")

    foreach(_svg IN LISTS _svgs)
        get_filename_component(_svg_name "${_svg}" NAME_WE)
        # 输出头文件放在生成目录，命名与原始一致但替换为 *_svg.gen.h
        set(_out "${_GEN_DIR}/${_P}_${_svg_name}_svg.gen.h")

        # 自定义命令：当 svg 更新时，重新生成头文件
        add_custom_command(
                OUTPUT "${_out}"
                COMMAND "${CMAKE_COMMAND}"
                -DINPUT="${_svg}"
                -DOUTPUT="${_out}"
                -DPLATFORM="${_P}"
                -DNAME="${_svg_name}"
                -P "${_SVG_2_HDR}"
                DEPENDS "${_svg}" "${_SVG_2_HDR}"
                COMMENT "[platform/export] Generating ${_out}"
                VERBATIM
        )

        list(APPEND _EXPORT_ICON_HEADERS "${_out}")
    endforeach()
endforeach()

# 将所有图标生成聚合到一个自定义目标，便于与库建立依赖关系
if(_EXPORT_ICON_HEADERS)
    add_custom_target(platform_export_icons ALL
            DEPENDS ${_EXPORT_ICON_HEADERS}
    )
endif()

# 3) 基于 PLATFORM_APIS 生成 register_platform_apis.gen.cpp（在配置阶段直接写出）
set(_API_INC "")
set(_API_REG "")
set(_API_UNREG "")
foreach(_P IN LISTS PLATFORM_APIS)
    string(APPEND _API_INC   "#include \"${_P}/api/api.h\"\n")
    string(APPEND _API_REG   "\tregister_${_P}_api();\n")
    string(APPEND _API_UNREG "\tunregister_${_P}_api();\n")
endforeach()

set(_REGISTER_CPP "${_GEN_DIR}/register_platform_apis.gen.cpp")
file(WRITE "${_REGISTER_CPP}" "/* Auto-generated. Do not edit. */\n")
file(APPEND "${_REGISTER_CPP}" "#include \"register_platform_apis.h\"\n\n")
file(APPEND "${_REGISTER_CPP}" "${_API_INC}\n")
file(APPEND "${_REGISTER_CPP}" "void register_platform_apis() {\n${_API_REG}}\n\n")
file(APPEND "${_REGISTER_CPP}" "void unregister_platform_apis() {\n${_API_UNREG}}\n")

# 4) 收集各平台的 api/*.cpp 源并创建静态库 platform
set(_PLATFORM_SOURCES "${_REGISTER_CPP}")
foreach(_P IN LISTS PLATFORM_APIS)
    file(GLOB _api_src CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/${_P}/api/*.cpp")
    list(APPEND _PLATFORM_SOURCES ${_api_src})
endforeach()

# 若没有任何源，也创建一个空库以保持接口一致（可被上层安全链接）
if(_PLATFORM_SOURCES STREQUAL "")
    # 创建一个虚拟占位源，避免空目标错误
    set(_DUMMY "${_GEN_DIR}/__platform_empty__.cpp")
    file(WRITE "${_DUMMY}" "/* empty */\n")
    list(APPEND _PLATFORM_SOURCES "${_DUMMY}")
endif()

add_library(platform STATIC ${_PLATFORM_SOURCES})

# 生成的图标头文件需要在编译前可用；建立依赖确保先生成（即使未直接作为源参与编译）
if(TARGET platform_export_icons)
    add_dependencies(platform platform_export_icons)
endif()

# 头文件搜索路径：当前源目录 + 生成目录（供包含生成的 *_svg.gen.h 与包含各平台的 api headers）
target_include_directories(platform
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${_GEN_DIR}"
)

# 将 C++ 标准与工程保持一致（Godot 使用 C++17）
set_target_properties(platform PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
)

# 至此，等价于：
# - SCons: 生成 export/*.svg 对应的 *_svg.gen.h
# - SCons: 生成 register_platform_apis.gen.cpp
# - SCons: 将各平台 {p}/api/*.cpp 编译为静态库，并提供给上层链接
#
# 上层只需：
#   target_link_libraries(<你的目标> PRIVATE platform)
# 即可获得相同能力。