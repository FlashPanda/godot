# CMake

# 说明：
# 本子目录的 CMakeLists.txt 用于实现与 SCons/SCsub 等价的功能：
#   - 收集当前目录下一层子目录中的所有 *.cs（对应 Glob("*/*.cs")）
#   - 调用 Python 中的 editor.template_builders.make_templates 生成 templates.gen.h
#   - 暴露一个与 SCons Alias("editor_template_cs", ...) 等价的自定义目标 editor_template_cs
#
# 注意：
# 1) 我们通过 Python3 解释器 import editor.template_builders 并调用 make_templates。
#    为了健壮性，使用一个在构建目录生成的小型 Python 驱动脚本来调用该函数，
#    避免命令行转义带来的跨平台问题。
# 2) 生成的头文件放在当前子目录的构建输出目录（CMAKE_CURRENT_BINARY_DIR）下，
#    变量名为 GENERATED_HEADER，父作用域可通过 EDITOR_TEMPLATE_CS_HEADER 获取该路径。
# 3) 该子目录只负责“生成文件”，不直接将其加入任何库或可执行目标，保持与 SCons 行为一致。
#    如需将 templates.gen.h 纳入某上层目标，请在父目录中 include_directories(${CMAKE_CURRENT_BINARY_DIR})
#    或者 target_include_directories(...)，并将 EDITOR_TEMPLATE_CS_HEADER 加入相应目标的源/依赖。

# 需要 Python3 解释器
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# 收集一层子目录内的 *.cs（等价于 SCons 的 Glob("*/*.cs")）
# 示例匹配：./Foo/*.cs、./Bar/*.cs（不会递归更深层）
file(GLOB CS_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*/*.cs")

# 生成的头文件放到构建目录
set(GENERATED_HEADER "${CMAKE_CURRENT_BINARY_DIR}/templates.gen.h")

# 在构建目录写入一个小的 Python 驱动脚本，用来调用 editor.template_builders.make_templates
# 该脚本的职责：
#   - 将工程根目录（或你需要的模块根）添加到 sys.path，确保能 import editor.template_builders
#   - 以与 SCons 类似的签名尝试调用：make_templates(target, source, env?)，其中：
#       target: [输出文件路径字符串]
#       source: [输入 .cs 文件路径字符串列表]
#       env   : 可为 None（如果函数不需要会忽略）
set(_GENERATOR_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/make_cs_templates.py")
file(WRITE "${_GENERATOR_SCRIPT}"
        "#!/usr/bin/env python3
import argparse
import importlib
import os
import sys

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', required=True, help='Output header file path (templates.gen.h)')
    parser.add_argument('--module-root', required=True, help='Root to add into sys.path for importing editor.template_builders')
    parser.add_argument('sources', nargs='*', help='Input .cs files')
    args = parser.parse_args()

    # 确保可以 import editor.template_builders
    sys.path.insert(0, os.path.abspath(args.module_root))

    try:
        mod = importlib.import_module('editor.template_builders')
    except Exception as e:
        print('Failed to import editor.template_builders:', e, file=sys.stderr)
        sys.exit(1)

    target = [args.out]
    source = list(args.sources)

    # 兼容不同签名：优先尝试 (target, source, env)，否则尝试 (target, source)
    try:
        mod.make_templates(target, source, None)  # 与 SCons 常见签名对齐
    except TypeError:
        mod.make_templates(target, source)

if __name__ == '__main__':
    main()
")

# 确保脚本可执行（非必需，但在类 UNIX 系统上更友好）
if(UNIX)
    execute_process(COMMAND ${CMAKE_COMMAND} -E chmod +x "${_GENERATOR_SCRIPT}")
endif()

# 声明自定义命令：从若干 .cs 生成 templates.gen.h
# - DEPENDS：所有 .cs 源文件改变会触发重新生成
# - BYPRODUCTS：标注生成的文件，利于多配置生成器跟踪产物
# - WORKING_DIRECTORY：切到当前源目录（如脚本对相对路径有假设时更稳妥）
add_custom_command(
        OUTPUT  "${GENERATED_HEADER}"
        BYPRODUCTS "${GENERATED_HEADER}"
        DEPENDS ${CS_SOURCES} "${_GENERATOR_SCRIPT}"
        COMMAND ${Python3_EXECUTABLE} "${_GENERATOR_SCRIPT}"
        --out "${GENERATED_HEADER}"
        --module-root "${CMAKE_SOURCE_DIR}"
        ${CS_SOURCES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating C# templates header: ${GENERATED_HEADER}"
        VERBATIM
)

# 与 SCons 的 Alias(\"editor_template_cs\", ...) 对齐：提供一个自定义目标
# 让上层可以通过 add_dependencies(某目标 editor_template_cs) 来确保先生成头文件。
add_custom_target(editor_template_cs
        DEPENDS "${GENERATED_HEADER}"
)

# 暴露生成的头文件路径给父作用域（可选）
set(EDITOR_TEMPLATE_CS_HEADER "${GENERATED_HEADER}" PARENT_SCOPE)

# 说明：
# - 若需要在上层目标中使用该头文件：
#     1) 在父目录 CMakeLists.txt 中 add_subdirectory(本子目录)
#     2) 将 ${CMAKE_CURRENT_BINARY_DIR} 加入包含目录：
#        target_include_directories(<your_target> PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
#     3) 可添加构建依赖，确保先生成：
#        add_dependencies(<your_target> editor_template_cs)