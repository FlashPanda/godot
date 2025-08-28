# 子目录：modules/CMakeLists.txt

cmake_minimum_required(VERSION 3.18)

# ---- 用户可配置变量（父级可传入） ----
# 1) 更精确：name=path;name2=path2（path 可相对本目录或绝对路径）
set(GODOT_MODULES_MAP "" CACHE STRING "Semicolon-separated list of name=path module entries")

# 2) 简略：仅名字列表；若 GODOT_MODULES_MAP 为空，则默认 path=name
set(GODOT_MODULES "" CACHE STRING "Semicolon-separated list of module names")

# 测试开关：生成 modules_tests.gen.h
option(GODOT_ENABLE_TESTS "Enable generation of modules_tests.gen.h" OFF)

# ---- 解析模块列表 ----
set(_MODULE_NAMES "")
set(_MODULE_PATHS "")

if(GODOT_MODULES_MAP)
    foreach(_pair IN LISTS GODOT_MODULES_MAP)
        string(FIND "${_pair}" "=" _eq)
        if(_eq EQUAL -1)
            message(FATAL_ERROR "GODOT_MODULES_MAP entry '${_pair}' must be in 'name=path' format")
        endif()
        string(SUBSTRING "${_pair}" 0 ${_eq} _name)
        math(EXPR _val_start "${_eq}+1")
        string(SUBSTRING "${_pair}" ${_val_start} -1 _path)

        if(NOT IS_ABSOLUTE "${_path}")
            set(_path "${CMAKE_CURRENT_SOURCE_DIR}/${_path}")
        endif()

        list(APPEND _MODULE_NAMES "${_name}")
        file(TO_CMAKE_PATH "${_path}" _path_norm)
        list(APPEND _MODULE_PATHS "${_path_norm}")
    endforeach()
elseif(GODOT_MODULES)
    foreach(_name IN LISTS GODOT_MODULES)
        list(APPEND _MODULE_NAMES "${_name}")
        set(_path "${CMAKE_CURRENT_SOURCE_DIR}/${_name}")
        file(TO_CMAKE_PATH "${_path}" _path_norm)
        list(APPEND _MODULE_PATHS "${_path_norm}")
    endforeach()
else()
    # 若用户均未指定，默认把当前目录下的一层子目录当作模块名
    file(GLOB _subdirs LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/*")
    foreach(_sd IN LISTS _subdirs)
        if(IS_DIRECTORY "${_sd}")
            get_filename_component(_name "${_sd}" NAME)
            list(APPEND _MODULE_NAMES "${_name}")
            file(TO_CMAKE_PATH "${_sd}" _sd_norm)
            list(APPEND _MODULE_PATHS "${_sd_norm}")
        endif()
    endforeach()
endif()

# 名称与路径个数核对
list(LENGTH _MODULE_NAMES _N1)
list(LENGTH _MODULE_PATHS _N2)
if(NOT _N1 EQUAL _N2)
    message(FATAL_ERROR "Module names and paths list lengths mismatch.")
endif()

# ---- 生成目录 ----
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

# ---- 生成 modules_enabled.gen.h ----
set(_enabled_h "${GENERATED_DIR}/modules_enabled.gen.h")

set(_enabled_content "// Auto-generated. Do not edit manually.\n")
foreach(_idx RANGE 0 "$<IF:$<BOOL:${_N1}>,${_N1}-1,-1>")
    if(_idx LESS 0)
        break()
    endif()
    list(GET _MODULE_NAMES ${_idx} _mname)
    string(TOUPPER "${_mname}" _MNAME_UP)
    string(APPEND _enabled_content "#define MODULE_${_MNAME_UP}_ENABLED\n")
endforeach()

file(WRITE "${_enabled_h}" "${_enabled_content}")

# ---- 生成 register_module_types.gen.cpp ----
set(_reg_cpp "${GENERATED_DIR}/register_module_types.gen.cpp")

# 收集 include 和 init/uninit 片段
set(_includes "")
set(_inits "")
set(_uninits "")

foreach(_idx RANGE 0 "$<IF:$<BOOL:${_N1}>,${_N1}-1,-1>")
    if(_idx LESS 0)
        break()
    endif()
    list(GET _MODULE_NAMES ${_idx} _mname)
    list(GET _MODULE_PATHS ${_idx} _mpath)
    string(TOUPPER "${_mname}" _MNAME_UP)

    # 头文件包含路径，尽量相对当前 modules 目录，使 include 与源布局接近 SCons
    file(RELATIVE_PATH _rel_from_mod "${CMAKE_CURRENT_SOURCE_DIR}" "${_mpath}")
    if(_rel_from_mod MATCHES "^[.][.]/") # 如果不在 modules 下，就用绝对或规范化路径
        set(_inc_path "${_mpath}/register_types.h")
    else()
        set(_inc_path "${_rel_from_mod}/register_types.h")
    endif()
    file(TO_CMAKE_PATH "${_inc_path}" _inc_norm)

    string(APPEND _includes "#include \"${_inc_norm}\"\n")
    string(APPEND _inits
            "#ifdef MODULE_${_MNAME_UP}_ENABLED
\tinitialize_${_mname}_module(p_level);
#endif
")
    string(APPEND _uninits
            "#ifdef MODULE_${_MNAME_UP}_ENABLED
\tuninitialize_${_mname}_module(p_level);
#endif
")
endforeach()

set(_reg_cpp_content
        "#include \"register_module_types.h\"
#include \"modules_enabled.gen.h\"

${_includes}
void initialize_modules(ModuleInitializationLevel p_level) {
${_inits}}
void uninitialize_modules(ModuleInitializationLevel p_level) {
${_uninits}}")

# 写入文件
file(WRITE "${_reg_cpp}" "${_reg_cpp_content}")

# ---- 可选生成 modules_tests.gen.h ----
set(_tests_h "")
if(GODOT_ENABLE_TESTS)
    set(_tests_h "${GENERATED_DIR}/modules_tests.gen.h")
    set(_tests_content "// Auto-generated. Do not edit manually.\n")
    foreach(_idx RANGE 0 "$<IF:$<BOOL:${_N1}>,${_N1}-1,-1>")
        if(_idx LESS 0)
            break()
        endif()
        list(GET _MODULE_PATHS ${_idx} _mpath)
        file(GLOB _test_headers "${_mpath}/tests/*.h")
        foreach(_th IN LISTS _test_headers)
            file(TO_CMAKE_PATH "${_th}" _th_norm)
            string(APPEND _tests_content "#include \"${_th_norm}\"\n")
        endforeach()
    endforeach()
    file(WRITE "${_tests_h}" "${_tests_content}")
endif()

# ---- 将生成文件加入目标或库 ----
# 为 register_module_types 单独建静态库，贴近 SCons 的 libmodules.a 语义
add_library(modules STATIC "${_reg_cpp}")
target_include_directories(modules PUBLIC "${GENERATED_DIR}")
# 供其它代码包含 #include "modules_enabled.gen.h"
# 若有需要，也可对 godot 目标追加 include 目录

# ---- 遍历模块：优先使用其自带 CMakeLists，否则回退为简单静态库 ----
set(_MODULE_LIBS "")
foreach(_idx RANGE 0 "$<IF:$<BOOL:${_N1}>,${_N1}-1,-1>")
    if(_idx LESS 0)
        break()
    endif()
    list(GET _MODULE_NAMES ${_idx} _mname)
    list(GET _MODULE_PATHS ${_idx} _mpath)

    set(_cmakelists "${_mpath}/CMakeLists.txt")
    if(EXISTS "${_cmakelists}")
        # 让子模块自行定义其库，假设其最终会定义 module_<name> 或向 godot 追加源
        add_subdirectory("${_mpath}" "${CMAKE_CURRENT_BINARY_DIR}/modules/${_mname}")
        # 尝试收集常见命名的库
        if(TARGET "module_${_mname}")
            list(APPEND _MODULE_LIBS "module_${_mname}")
        endif()
    else()
        # 回退：简单把该目录下 *.cpp 编为静态库 module_<name>
        file(GLOB _mod_srcs CONFIGURE_DEPENDS "${_mpath}/*.cpp")
        if(_mod_srcs)
            add_library("module_${_mname}" STATIC ${_mod_srcs})
            target_include_directories("module_${_mname}" PUBLIC "${_mpath}")
            list(APPEND _MODULE_LIBS "module_${_mname}")
        endif()
    endif()
endforeach()

# ---- 连接到 godot 目标（若存在） ----
if(TARGET godot)
    # 链接顺序：先各 module_<name>，后 modules（包含 register_module_types）
    if(_MODULE_LIBS)
        target_link_libraries(godot PRIVATE ${_MODULE_LIBS})
    endif()
    target_link_libraries(godot PRIVATE modules)

    # 确保生成目录可见（包含 modules_enabled.gen.h）
    target_include_directories(godot PRIVATE "${GENERATED_DIR}")
endif()

# 对外暴露生成头路径，便于测试目标或其他子目录使用
set(GODOT_MODULES_GENERATED_DIR "${GENERATED_DIR}" PARENT_SCOPE)
if(GODOT_ENABLE_TESTS AND _tests_h)
    set(GODOT_MODULES_TESTS_HEADER "${_tests_h}" PARENT_SCOPE)
endif()