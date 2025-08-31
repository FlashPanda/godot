# 子目录 CMakeLists.txt —— 通用的 “生成 bundle” 能力
# 使用方式（示例）：
#  1) 父级调用 add_subdirectory(此目录)
#  2) 在父级或本文件通过 set(...) 设定以下变量：
#     - BUNDLE_NAME              生成产物的逻辑名称（仅用于目标名/消息展示）
#     - BUNDLE_OUT               产物输出路径（文件/目录），若未指定则默认生成 <binary dir>/<BUNDLE_NAME>.bundle
#     - BUNDLE_FILES             参与打包的输入文件（LIST）。若未指定将回退为当前目录下的 assets/* 全部文件
#     - BUNDLE_PROG              具体打包使用的程序/脚本（例如 Python 或自定义可执行文件）
#     - BUNDLE_ARGS              传给 BUNDLE_PROG 的固定参数（LIST，可为空）
#     - BUNDLE_ALWAYS            是否每次构建都重新打包（默认 OFF）
#  3) 调用 generate_bundle(<target>) 定义打包命令和目标
#
# 变量说明（对应 SCons 里一些常见命名）：
#   - files  -> BUNDLE_FILES
#   - prog   -> BUNDLE_PROG
#   - generate_bundle_command -> 本文件里会根据 BUNDLE_PROG/BUNDLE_ARGS/BUNDLE_FILES 组装出 COMMAND 列表
#   - command -> add_custom_command/add_custom_target 的语义实现
#
# 你可以按需把 BUNDLE_PROG 指向你的脚本，并在 BUNDLE_ARGS 中填写脚本所需参数；
# BUNDLE_FILES 列表会在命令末尾追加，从而形成完整的打包命令行。

cmake_minimum_required(VERSION 3.20)

# 允许在父级或命令行传入的开关/变量（全部是可选，若未设置则给出合理默认）
set(BUNDLE_NAME   ""         CACHE STRING "逻辑名称，仅用于目标名/提示信息")
set(BUNDLE_OUT    ""         CACHE STRING "打包输出路径（文件或目录路径）")
set(BUNDLE_PROG   ""         CACHE STRING "用于打包的程序或脚本（例如 python、node、或自定义可执行文件）")
set(BUNDLE_ARGS   ""         CACHE STRING "传给 BUNDLE_PROG 的附加参数（使用分号分隔表示列表）")
set(BUNDLE_FILES  ""         CACHE STRING "打包输入文件列表（使用分号分隔表示列表）")
set(BUNDLE_ALWAYS OFF        CACHE BOOL   "是否每次构建都重新打包（不走输出文件时间戳增量）")

# 若未指定 BUNDLE_NAME，则用当前目录名作为默认名称
if(NOT BUNDLE_NAME)
    get_filename_component(_default_name "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
    set(BUNDLE_NAME "${_default_name}")
endif()

# 若未指定 BUNDLE_OUT，则在构建目录生成一个 <name>.bundle 作为输出
if(NOT BUNDLE_OUT)
    set(BUNDLE_OUT "${CMAKE_CURRENT_BINARY_DIR}/${BUNDLE_NAME}.bundle")
endif()

# 若未显式提供输入文件列表，则回退为收集本目录 assets/* 的所有文件
# 注意：你可以根据实际需要修改这一默认收集规则（如 *.plist、*.icns、*.json、resources/** 等）
if(NOT BUNDLE_FILES)
    file(GLOB_RECURSE _bundle_files
            CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/*"
    )
    set(BUNDLE_FILES "${_bundle_files}")
endif()

# 组装最终命令行（generate_bundle_command）
# 规则：
#   COMMAND: <BUNDLE_PROG> <BUNDLE_ARGS...> <BUNDLE_OUT> <BUNDLE_FILES...>
# 你可以根据自己的脚本约定把 <BUNDLE_OUT> 放在前/后，这里将其置于 args 与 files 之间，较为通用。
#
# 说明：
# - 如果未提供 BUNDLE_PROG，则使用 CMake 自带的 -E tar 作为一个通用 fallback（输出 .tar）
# - 如需其它格式（如 .zip），可将 BUNDLE_PROG 指向 zip 程序或自定义脚本
set(_use_fallback OFF)
if(BUNDLE_PROG)
    # 显式指定了打包程序
    set(_cmd "${BUNDLE_PROG}")
    if(BUNDLE_ARGS)
        list(APPEND _cmd ${BUNDLE_ARGS})
    endif()
    # 通用做法：把输出路径作为一个位置参数传入（你的脚本需要认识它）
    list(APPEND _cmd "${BUNDLE_OUT}")
    if(BUNDLE_FILES)
        list(APPEND _cmd ${BUNDLE_FILES})
    endif()
else()
    # Fallback：使用 cmake -E tar 创建一个 .tar 包
    # 若 BUNDLE_OUT 没有 .tar 后缀，自动补上
    get_filename_component(_out_ext "${BUNDLE_OUT}" EXT)
    if(NOT _out_ext STREQUAL ".tar")
        set(BUNDLE_OUT "${BUNDLE_OUT}.tar")
    endif()
    set(_use_fallback ON)

    # cmake -E tar <c|x|t>[vfjJ] file.tar [--] [<pathname>...]
    # 我们用 'cvf' 表示 create + verbose + filename
    set(_cmd "${CMAKE_COMMAND}" -E tar "cvf" "${BUNDLE_OUT}")
    if(BUNDLE_FILES)
        list(APPEND _cmd ${BUNDLE_FILES})
    else()
        # 没有文件则给出一个提示文件，避免空 tar
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/__empty.txt" "empty bundle\n")
        list(APPEND _cmd "${CMAKE_CURRENT_BINARY_DIR}/__empty.txt")
    endif()
endif()

# 定义一个函数，用于生成打包命令和目标（等同于 SCons 的 generate_bundle(...)）
# 用法：generate_bundle(<target_name>)
function(generate_bundle target_name)
    if(NOT target_name)
        message(FATAL_ERROR "generate_bundle 需要一个 target 名称参数")
    endif()

    # 为了尽可能触发增量构建，我们将 BUNDLE_OUT 作为 OUTPUT
    # 当 BUNDLE_ALWAYS=ON 时，我们刻意打破增量（借助 COMMAND_EXPAND_LISTS + 空输出后删除重建）
    set(_outputs "${BUNDLE_OUT}")

    # 当输出目录形式时，建议清空后重建，保证内容一致
    # 使用 cmake -E remove / remove_directory + make_directory
    # 若是文件（如 .bundle/.tar/.zip），直接覆盖即可
    # 这里用一个简化策略：无论文件还是目录，都先 remove，再执行命令
    add_custom_command(
            OUTPUT  ${_outputs}
            COMMAND ${CMAKE_COMMAND} -E remove -f "${BUNDLE_OUT}"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${BUNDLE_OUT}"
            COMMAND ${_cmd}
            BYPRODUCTS "${BUNDLE_OUT}"
            DEPENDS ${BUNDLE_FILES}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "[${BUNDLE_NAME}] Generating bundle -> ${BUNDLE_OUT}"
            VERBATIM
            COMMAND_EXPAND_LISTS
    )

    # 定义可构建目标
    add_custom_target(${target_name} DEPENDS ${_outputs})

    # 如果用户希望“每次都重打包”，那就附加一个伪命令让输出失效
    if(BUNDLE_ALWAYS)
        add_custom_command(
                TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E touch_nocreate "${CMAKE_CURRENT_BINARY_DIR}/.${target_name}.stamp"
                COMMENT "[${BUNDLE_NAME}] Forcing rebuild each time (BUNDLE_ALWAYS=ON)."
                VERBATIM
        )
    endif()

    # 对外回传一些信息（可选）
    set(${target_name}_OUT    "${BUNDLE_OUT}"    PARENT_SCOPE)
    set(${target_name}_FILES  "${BUNDLE_FILES}"  PARENT_SCOPE)
    set(${target_name}_PROG   "${BUNDLE_PROG}"   PARENT_SCOPE)
endfunction()

# 你可以在这里或父级里调用：
#   generate_bundle(<your_bundle_target_name>)
#
# 例如：
#   generate_bundle(bundle)
#
# 若希望在构建主可执行后再打包，可在父级中添加：
#   add_dependencies(bundle <your_main_target>)
#
# 注意：本文件仅提供“生成 bundle”的通用机制，具体打包行为完全由
# BUNDLE_PROG + BUNDLE_ARGS + BUNDLE_FILES 决定；未提供时使用一个通用 tar Fallback。