# 该子CMakeLists不会声明 project()，仅定义本子目录目标及其生成规则

# ------------- 可配置项（供上层或命令行 -D 传入） -------------
# Mesa 根路径（外部预置生成/源码包）
set(GODOT_MESA_LIBS "" CACHE PATH "Path to folder containing godot-mesa (i.e. <mesa_libs>/godot-mesa)")
# Windows Agility SDK 支持
set(AGILITY_SDK_PATH "" CACHE PATH "Path to Microsoft D3D12 Agility SDK (optional)")
set(AGILITY_SDK_MULTIARCH_ENABLED OFF CACHE BOOL "Enable Agility SDK multiarch")
# PIX 支持
set(USE_PIX OFF CACHE BOOL "Enable PIX")
set(PIX_PATH "" CACHE PATH "Path to PIX installation (must contain Include/)")
# Python 解释器用于生成 mesa 文件
find_package(Python3 COMPONENTS Interpreter QUIET)
if (NOT Python3_Interpreter_FOUND)
    set(Python3_EXECUTABLE "python" CACHE STRING "Python executable")
endif()

# ------------- 目标与源文件 -------------
# 当前目录下所有 .cpp
file(GLOB D3D12_RDD_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 如果上层需要以对象库参与更大目标，可改为 OBJECT
add_library(d3d12_rdd STATIC ${D3D12_RDD_SOURCES})

# ------------- 包含目录优先级（等价 SCons 中的 Prepend） -------------
# DirectX-Headers（优先于 Windows SDK）
set(DIRECTX_HEADERS_DIR "${CMAKE_SOURCE_DIR}/thirdparty/directx_headers/include")
target_include_directories(d3d12_rdd
        BEFORE PRIVATE
        "${DIRECTX_HEADERS_DIR}/directx"
        "${DIRECTX_HEADERS_DIR}/dxguids"
)

# D3D12 Memory Allocator
target_include_directories(d3d12_rdd
        PRIVATE "${CMAKE_SOURCE_DIR}/thirdparty/d3d12ma"
)

# PIX（可选）
if (USE_PIX AND EXISTS "${PIX_PATH}/Include")
    target_compile_definitions(d3d12_rdd PRIVATE PIX_ENABLED)
    target_include_directories(d3d12_rdd PRIVATE "${PIX_PATH}/Include")
endif()

# Agility SDK（可选）
if (AGILITY_SDK_PATH AND EXISTS "${AGILITY_SDK_PATH}")
    target_compile_definitions(d3d12_rdd PRIVATE AGILITY_SDK_ENABLED)
    if (AGILITY_SDK_MULTIARCH_ENABLED)
        target_compile_definitions(d3d12_rdd PRIVATE AGILITY_SDK_MULTIARCH_ENABLED)
    endif()
endif()

# ------------- Mesa（SPIR-V -> DXIL）生成与包含 -------------
if (GODOT_MESA_LIBS)
    # 目录规约
    file(TO_CMAKE_PATH "${GODOT_MESA_LIBS}/godot-mesa"        mesa_dir)
    file(TO_CMAKE_PATH "${GODOT_MESA_LIBS}/godot-mesa/generated" mesa_gen_dir)

    # 自定义生成规则（对应 SCsub 的 custom_build_steps）
    # 三元组：子目录、命令模板（带 %s 表示输出目录）、生成的文件名
    set(_MESA_STEPS
            "src/compiler;glsl/ir_expression_operation.py enum > %s/ir_expression_operation.h;ir_expression_operation.h"
            "src/compiler/nir;nir_builder_opcodes_h.py > %s/nir_builder_opcodes.h;nir_builder_opcodes.h"
            "src/compiler/nir;nir_constant_expressions.py > %s/nir_constant_expressions.c;nir_constant_expressions.c"
            "src/compiler/nir;nir_intrinsics_h.py --outdir %s;nir_intrinsics.h"
            "src/compiler/nir;nir_intrinsics_c.py --outdir %s;nir_intrinsics.c"
            "src/compiler/nir;nir_intrinsics_indices_h.py --outdir %s;nir_intrinsics_indices.h"
            "src/compiler/nir;nir_opcodes_h.py > %s/nir_opcodes.h;nir_opcodes.h"
            "src/compiler/nir;nir_opcodes_c.py > %s/nir_opcodes.c;nir_opcodes.c"
            "src/compiler/nir;nir_opt_algebraic.py > %s/nir_opt_algebraic.c;nir_opt_algebraic.c"
            "src/compiler/spirv;vtn_generator_ids_h.py spir-v.xml %s/vtn_generator_ids.h;vtn_generator_ids.h"
            "src/microsoft/compiler;dxil_nir_algebraic.py -p ../../../src/compiler/nir > %s/dxil_nir_algebraic.c;dxil_nir_algebraic.c"
            "src/util;format_srgb.py > %s/format_srgb.c;format_srgb.c"
            "src/util/format;u_format_table.py u_format.csv --header > %s/u_format_pack.h;u_format_pack.h"
            "src/util/format;u_format_table.py u_format.csv > %s/u_format_table.c;u_format_table.c"
    )

    set(MESA_GENERATED_FILES "")
    set(_MESA_GEN_INCLUDE_DIRS "${mesa_gen_dir}/src")

    # 小工具：平台无关“带重定向”的命令
    function(_add_redirect_command out_var work_dir cmd_str out_file out_dir)
        if (WIN32)
            # 用 cmd /C 让重定向工作在 Windows
            add_custom_command(
                    OUTPUT "${out_file}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
                    COMMAND cmd /C "${cmd_str}"
                    WORKING_DIRECTORY "${work_dir}"
                    COMMENT "[mesa] ${cmd_str}"
                    VERBATIM
            )
        else()
            # 非 Windows 使用 sh -c
            add_custom_command(
                    OUTPUT "${out_file}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
                    COMMAND sh -c "${cmd_str}"
                    WORKING_DIRECTORY "${work_dir}"
                    COMMENT "[mesa] ${cmd_str}"
                    VERBATIM
            )
        endif()
        set(${out_var} "${${out_var}};${out_file}" PARENT_SCOPE)
    endfunction()

    foreach(step IN LISTS _MESA_STEPS)
        string(REPLACE ";" "|" step_piped "${step}") # 避免 list() 冲突
        string(REPLACE "|" ";" parts "${step_piped}")
        list(GET parts 0 subdir)
        list(GET parts 1 cmdtpl)
        list(GET parts 2 genfile)

        set(in_dir  "${mesa_dir}/${subdir}")
        set(out_dir "${mesa_gen_dir}/${subdir}")
        set(out_file "${out_dir}/${genfile}")

        # 头文件的生成路径需要加入包含目录
        if (genfile MATCHES "\\.h$")
            list(APPEND _MESA_GEN_INCLUDE_DIRS "${out_dir}")
        endif()

        # 将模板中的 %s 替换为 out_dir（注意路径带空格时引用）
        # 同时在命令前缀加上 Python 解释器
        # 模板里已经含有脚本名与参数（有的使用 > 输出重定向）
        # 这里将 cmdtpl 前置 "python "，并将 %s 替换为带引号的 out_dir
        set(cmd_work "${cmdtpl}")
        string(REPLACE "%s" "\"${out_dir}\"" cmd_work "${cmd_work}")
        # 某些模板直接以 "nir_xxx.py ..." 开头，需要补上 Python 可执行程序
        set(full_cmd "\"${Python3_EXECUTABLE}\" ${cmd_work}")

        # 生成命令
        _add_redirect_command(MESA_GENERATED_FILES "${in_dir}" "${full_cmd}" "${out_file}" "${out_dir}")
    endforeach()

    # 聚合生成目标
    add_custom_target(mesa_gen ALL DEPENDS ${MESA_GENERATED_FILES})

    # 让 d3d12_rdd 目标在编译前确保 mesa 生成完成
    add_dependencies(d3d12_rdd mesa_gen)

    # 包含目录：生成产物与私有源码（按需精简）
    # 生成产物（generated/src 以及头文件落地目录）
    list(REMOVE_DUPLICATES _MESA_GEN_INCLUDE_DIRS)
    target_include_directories(d3d12_rdd PRIVATE ${_MESA_GEN_INCLUDE_DIRS})

    # 将 mesa 的私有树加入 include（与 SCons 类似的“盲加”）
    # 为避免过度复杂的目录枚举，至少包含根 src（多数内部 include 可解析）
    if (EXISTS "${mesa_dir}/src")
        target_include_directories(d3d12_rdd PRIVATE "${mesa_dir}/src")
    endif()
    # 黑名单（与 SCons 对齐，避免 src/c11）
    if (EXISTS "${mesa_dir}/src/c11")
        # 不加入该路径
    endif()
endif()

# ------------- 额外宏定义（与 SCsub 对齐） -------------
# Added by ourselves
target_compile_definitions(d3d12_rdd PRIVATE WINDOWS_NO_FUTEX)

# 读取 Mesa 版本，拼接 PACKAGE_VERSION
if (GODOT_MESA_LIBS)
    set(_MESA_VERSION_FILE "${mesa_dir}/VERSION.info")
    if (NOT EXISTS "${_MESA_VERSION_FILE}")
        set(_MESA_VERSION_FILE "${mesa_dir}/VERSION")
    endif()
    if (EXISTS "${_MESA_VERSION_FILE}")
        file(READ "${_MESA_VERSION_FILE}" _mesa_ver_raw)
        string(STRIP "${_mesa_ver_raw}" _mesa_ver)
        target_compile_definitions(d3d12_rdd PRIVATE
                __STDC_CONSTANT_MACROS
                __STDC_FORMAT_MACROS
                __STDC_LIMIT_MACROS
                PIPE_SUBSYSTEM_WINDOWS_USER
                _Static_assert=static_assert
                PACKAGE_VERSION="${_mesa_ver}"
                PACKAGE_BUGREPORT="https://gitlab.freedesktop.org/mesa/mesa/-/issues"
        )
    else()
        target_compile_definitions(d3d12_rdd PRIVATE
                __STDC_CONSTANT_MACROS
                __STDC_FORMAT_MACROS
                __STDC_LIMIT_MACROS
                PIPE_SUBSYSTEM_WINDOWS_USER
                _Static_assert=static_assert
                PACKAGE_BUGREPORT="https://gitlab.freedesktop.org/mesa/mesa/-/issues"
        )
    endif()
endif()

# MSVC / 非 MSVC 差异定义
if (MSVC)
    target_compile_definitions(d3d12_rdd PRIVATE
            _USE_MATH_DEFINES
            VC_EXTRALEAN
            _CRT_SECURE_NO_WARNINGS
            _CRT_SECURE_NO_DEPRECATE
            _SCL_SECURE_NO_WARNINGS
            _SCL_SECURE_NO_DEPRECATE
            _ALLOW_KEYWORD_MACROS
            _HAS_EXCEPTIONS=0
            NOMINMAX
            HAVE_STRUCT_TIMESPEC
    )
else()
    target_compile_definitions(d3d12_rdd PRIVATE
            HAVE_STRUCT_TIMESPEC
    )
    # GCC < 13 忽略 unknown pragma（等价 SCons 的 -Wno-unknown-pragmas）
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
            target_compile_options(d3d12_rdd PRIVATE -Wno-unknown-pragmas)
        endif()
    endif()
endif()

# ------------- 其它小项 -------------
# 允许上层看到该目标的包含（如果需要链接它，保持 PRIVATE 即可）
# 无额外链接库（此处仅组织源码与生成物；真正链接到渲染后端由上层完成）
#
# 上层示例：
#   add_subdirectory(path/to/this)
#   target_link_libraries(upper_target PRIVATE d3d12_rdd)