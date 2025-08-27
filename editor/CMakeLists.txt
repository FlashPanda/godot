# editor/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

# 选项与输入变量
option(GODOT_EDITOR_BUILD "Build the editor library and its generated sources" ON)
set(GODOT_PLATFORM_EXPORTERS "" CACHE STRING "Semicolon-separated exporter platforms, e.g. windows;linux;macos;android;ios")
set(GODOT_DOC_CLASS_PATH "" CACHE STRING "Semicolon-separated NAME=PATH entries; PATH can be relative to project root or absolute")
set(PYTHON_EXECUTABLE "" CACHE FILEPATH "Python to run real generators (optional)")

# 提前准备生成目录
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")

# 小工具：写出一个 CMake 脚本文件（供 add_custom_command -P 执行）
function(_write_gen_script out_path script_content)
    file(WRITE "${out_path}" "${script_content}")
endfunction()

# 解析 GODOT_DOC_CLASS_PATH，得到键值对与目录集合
set(_doc_pairs "")  # 形如 name|abs_path 的列表
set(_doc_dirs  "")  # 目录绝对路径
foreach(_entry IN LISTS GODOT_DOC_CLASS_PATH)
    if(_entry MATCHES "^([^=]+)=(.+)$")
        set(_name "${CMAKE_MATCH_1}")
        set(_path_raw "${CMAKE_MATCH_2}")
        if(IS_ABSOLUTE "${_path_raw}")
            set(_abs "${_path_raw}")
        else()
            set(_abs "${CMAKE_SOURCE_DIR}/${_path_raw}")
        endif()
        list(APPEND _doc_pairs "${_name}|${_abs}")
        list(APPEND _doc_dirs  "${_abs}")
    endif()
endforeach()

# 生成 doc_data_class_path.gen.h（从 _doc_pairs 写入 name/path 数组）
set(_doc_class_path_h "${CMAKE_CURRENT_BINARY_DIR}/generated/doc_data_class_path.gen.h")
set(_gen_doc_class_path_script "${CMAKE_CURRENT_BINARY_DIR}/gen_doc_class_path.cmake")
_write_gen_script("${_gen_doc_class_path_script}"
        "if(NOT DEFINED OUT)\n  message(FATAL_ERROR \"OUT not set\")\nendif()\n
if(NOT DEFINED PAIRS)\n  set(PAIRS \"\")\nendif()\n
# 解析 PAIRS: name|path;name2|path2;...\n
string(REPLACE \";\" \"|;\" _tmp \"${PAIRS}\")\n
set(_names \"\")\n
set(_paths \"\")\n
set(_items ${_tmp})\n
foreach(_i IN LISTS _items)\n
  if(_i MATCHES \"^([^|]+)\\|(.*)$\")\n
    list(APPEND _names \"${CMAKE_MATCH_1}\")\n
    list(APPEND _paths \"${CMAKE_MATCH_2}\")\n
  endif()\n
endforeach()\n
list(LENGTH _names _count)\n
file(WRITE \"${OUT}\" \"static const int _doc_data_class_path_count = ${_count};\\n\\n\")\n
file(APPEND \"${OUT}\" \"struct _DocDataClassPath {\\n\\tconst char *name;\\n\\tconst char *path;\\n};\\n\\n\")\n
math(EXPR _arr_size \"${_count} + 1\")\n
file(APPEND \"${OUT}\" \"static const _DocDataClassPath _doc_data_class_paths[${_arr_size}] = {\\n\")\n
list(LENGTH _names _nlen)\n
math(EXPR _last \"${_nlen} - 1\")\n
foreach(_idx RANGE 0 ${_last})\n
  list(GET _names ${_idx} _n)\n
  list(GET _paths ${_idx} _p)\n
  file(APPEND \"${OUT}\" \"\\t{\\\"${_n}\\\", \\\"${_p}\\\"},\\n\")\n
endforeach()\n
file(APPEND \"${OUT}\" \"\\t{nullptr, nullptr},\\n};\\n\")\n
")
add_custom_command(
        OUTPUT "${_doc_class_path_h}"
        COMMAND "${CMAKE_COMMAND}" -DOUT="${_doc_class_path_h}" -DPAIRS="${_doc_pairs}" -P "${_gen_doc_class_path_script}"
        COMMENT "Generating doc_data_class_path.gen.h"
        VERBATIM
)

# 生成 register_exporters.gen.cpp（根据 GODOT_PLATFORM_EXPORTERS）
set(_register_exporters_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/register_exporters.gen.cpp")
set(_gen_exporters_script "${CMAKE_CURRENT_BINARY_DIR}/gen_register_exporters.cmake")
_write_gen_script("${_gen_exporters_script}"
        "if(NOT DEFINED OUT)\n  message(FATAL_ERROR \"OUT not set\")\nendif()\n
if(NOT DEFINED PLATFORMS)\n  set(PLATFORMS \"\")\nendif()\n
set(_incs \"\")\n
set(_regs \"\")\n
set(_types \"\")\n
foreach(p IN LISTS PLATFORMS)\n
  string(APPEND _incs \"#include \\\"platform/${p}/export/export.h\\\"\\n\")\n
  string(APPEND _regs \"\\tregister_${p}_exporter();\\n\")\n
  string(APPEND _types \"\\tregister_${p}_exporter_types();\\n\")\n
endforeach()\n
file(WRITE \"${OUT}\" \"#include \\\"register_exporters.h\\\"\\n\\n\")\n
file(APPEND \"${OUT}\" \"${_incs}\\n\")\n
file(APPEND \"${OUT}\" \"void register_exporters() {\\n${_regs}}\\n\\n\")\n
file(APPEND \"${OUT}\" \"void register_exporter_types() {\\n${_types}}\\n\")\n
")
add_custom_command(
        OUTPUT "${_register_exporters_cpp}"
        COMMAND "${CMAKE_COMMAND}" -DOUT="${_register_exporters_cpp}" -DPLATFORMS="${GODOT_PLATFORM_EXPORTERS}" -P "${_gen_exporters_script}"
        COMMENT "Generating register_exporters.gen.cpp"
        VERBATIM
)

# 收集核心 API 文档与模块文档（*.xml）
set(_doc_xml_files "")
file(GLOB _core_docs CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/doc/classes/*.xml")
list(APPEND _doc_xml_files ${_core_docs})
foreach(_d IN LISTS _doc_dirs)
    if(EXISTS "${_d}")
        file(GLOB _mod_docs CONFIGURE_DEPENDS "${_d}/*.xml")
        list(APPEND _doc_xml_files ${_mod_docs})
    endif()
endforeach()

# 文档压缩头（占位生成；如需真实生成，可在此调用 PYTHON_EXECUTABLE 自定义脚本）
set(_doc_data_compressed_h "${CMAKE_CURRENT_BINARY_DIR}/generated/doc_data_compressed.gen.h")
set(_gen_docs_script "${CMAKE_CURRENT_BINARY_DIR}/gen_doc_data_compressed.cmake")
_write_gen_script("${_gen_docs_script}"
        "if(NOT DEFINED OUT)\n  message(FATAL_ERROR \"OUT not set\")\nendif()\n
if(NOT DEFINED INPUTS)\n  set(INPUTS \"\")\nendif()\n
# 占位实现：只写入文件数量与标记，保证可编译\n
list(LENGTH INPUTS _cnt)\n
file(WRITE \"${OUT}\" \"// Placeholder generated by CMake; inputs=${_cnt}\\n\\n\")\n
file(APPEND \"${OUT}\" \"static const char* g_doc_data_compressed_stub = \\\"docs embedded by build system\\\";\\n\")\n
")
add_custom_command(
        OUTPUT "${_doc_data_compressed_h}"
        COMMAND "${CMAKE_COMMAND}" -DOUT="${_doc_data_compressed_h}" -DINPUTS="${_doc_xml_files}" -P "${_gen_docs_script}"
        DEPENDS ${_doc_xml_files}
        COMMENT "Generating doc_data_compressed.gen.h (placeholder)"
        VERBATIM
)

# 各类翻译头（*.po -> 占位头）
function(_gen_po_header out_var rel_dir out_name)
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/generated/${out_name}")
    file(GLOB _po_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/${rel_dir}/*.po")
    set(_script "${CMAKE_CURRENT_BINARY_DIR}/gen_${out_name}.cmake")
    _write_gen_script("${_script}"
            "if(NOT DEFINED OUT)\n  message(FATAL_ERROR \"OUT not set\")\nendif()\n
if(NOT DEFINED INPUTS)\n  set(INPUTS \"\")\nendif()\n
list(LENGTH INPUTS _cnt)\n
file(WRITE \"${OUT}\" \"// Placeholder translations header; inputs=${_cnt}\\n\")\n
")
    add_custom_command(
            OUTPUT "${_out}"
            COMMAND "${CMAKE_COMMAND}" -DOUT="${_out}" -DINPUTS="${_po_files}" -P "${_script}"
            DEPENDS ${_po_files}
            COMMENT "Generating ${out_name} (placeholder)"
            VERBATIM
    )
    set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()

# Editor translations
_gen_po_header(_editor_translations_h "editor/translations/editor" "editor_translations.gen.h")
# Property translations
_gen_po_header(_property_translations_h "editor/translations/properties" "property_translations.gen.h")
# Documentation translations
_gen_po_header(_doc_translations_h "doc/translations" "doc_translations.gen.h")
# Extractable translations（包含 .po 与 extractable.pot）
set(_extractable_glob_dir "${CMAKE_SOURCE_DIR}/editor/translations/extractable")
file(GLOB _extractable_po CONFIGURE_DEPENDS "${_extractable_glob_dir}/*.po" "${_extractable_glob_dir}/extractable.pot")
set(_extractable_translations_h "${CMAKE_CURRENT_BINARY_DIR}/generated/extractable_translations.gen.h")
set(_gen_extractable_script "${CMAKE_CURRENT_BINARY_DIR}/gen_extractable_translations.cmake")
_write_gen_script("${_gen_extractable_script}"
        "if(NOT DEFINED OUT)\n  message(FATAL_ERROR \"OUT not set\")\nendif()\n
if(NOT DEFINED INPUTS)\n  set(INPUTS \"\")\nendif()\n
list(LENGTH INPUTS _cnt)\n
file(WRITE \"${OUT}\" \"// Placeholder extractable translations header; inputs=${_cnt}\\n\")\n
")
add_custom_command(
        OUTPUT "${_extractable_translations_h}"
        COMMAND "${CMAKE_COMMAND}" -DOUT="${_extractable_translations_h}" -DINPUTS="${_extractable_po}" -P "${_gen_extractable_script}"
        DEPENDS ${_extractable_po}
        COMMENT "Generating extractable_translations.gen.h (placeholder)"
        VERBATIM
)

# 本目录源码与平台 exporters 源码
file(GLOB _editor_local_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

set(_exporters_sources "")
foreach(e IN LISTS GODOT_PLATFORM_EXPORTERS)
    file(GLOB _exp_src CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/platform/${e}/export/*.cpp")
    list(APPEND _exporters_sources ${_exp_src})
endforeach()

# 进入子目录（存在才进入）
foreach(_sub IN ITEMS debugger export gui icons import plugins project_manager themes)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_sub}/CMakeLists.txt")
        add_subdirectory("${_sub}")
    endif()
endforeach()

# 生成库：editor
if(GODOT_EDITOR_BUILD)
    add_library(editor STATIC
            ${_editor_local_sources}
            ${_exporters_sources}
            "${_register_exporters_cpp}"
            "${_doc_class_path_h}"
            "${_doc_data_compressed_h}"
            "${_editor_translations_h}"
            "${_property_translations_h}"
            "${_doc_translations_h}"
            "${_extractable_translations_h}"
    )

    # 让编译器能找到生成头
    target_include_directories(editor PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )

    # 如需特定编译定义/选项，可在此添加
    # target_compile_definitions(editor PRIVATE EDITOR_ENABLED=1)

    # 如果子目录也产出库，可在此链接
    # target_link_libraries(editor PRIVATE <sub-libs>)
endif()