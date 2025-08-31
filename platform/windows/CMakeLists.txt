# 子目录 CMakeLists.txt —— 复刻 Windows 平台脚本核心逻辑（资源、程序、DLL 分发、别名等）
cmake_minimum_required(VERSION 3.20)

# 0) 仅在 Windows 下启用
if(NOT WIN32)
    message(STATUS "[win] 非 Windows 平台，跳过本子目录逻辑。")
    return()
endif()

# 1) 选项与变量映射（均可通过 -D 传入；给出合理默认）
# 程序与 wrap 控制
set(WIN_PROG_NAME               "godot"    CACHE STRING "主可执行目标名（等价 prog）")
option(WIN_ENABLE_WRAP          "是否构建 wrap 版本（等价 prog_wrap）" OFF)

# 源文件收集（等价 sources/common_win/common_win_wrap）
# 默认收集当前目录下所有 .cpp；如需严格控制，请在上层用 target_sources 覆盖或在此传入明确列表
file(GLOB _win_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
set(WIN_SOURCES                 "${_win_sources}"     CACHE STRING "主程序源文件列表（分号分隔）")
set(WIN_COMMON_SOURCES          ""                     CACHE STRING "Windows 通用源码（给主程序用，分号分隔）")
set(WIN_COMMON_WRAP_SOURCES     ""                     CACHE STRING "Windows 通用源码（给 wrap 程序用，分号分隔）")

# 资源文件与对应目标（等价 res_file/res_target/res_obj 以及 wrap 版本）
set(WIN_RES_FILE                ""                     CACHE FILEPATH ".rc 资源文件路径（主程序）")
set(WIN_RES_WRAP_FILE           ""                     CACHE FILEPATH ".rc 资源文件路径（wrap 程序）")

# 输出与目标目录（等价 target_dir/arch_bin_dir）
# 默认输出到 <binary_dir>/bin/<arch>（如 x86_64 或 arm64）
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_default_arch "x86_64")
else()
    set(_default_arch "x86")
endif()
# 允许外部指定更友好的标识
set(WIN_ARCH_BIN_DIR            "${_default_arch}"     CACHE STRING "架构名子目录（例如 x86_64/arm64）")
set(WIN_TARGET_DIR              "${CMAKE_BINARY_DIR}/bin" CACHE PATH "最终产物根目录")
set(_win_runtime_dir            "${WIN_TARGET_DIR}/${WIN_ARCH_BIN_DIR}")

# DXC / Agility / PIX 复制配置（等价 dxc_* / agility_* / pix_dll / dll）
# 源根目录与架构子目录
set(WIN_DXC_ROOT_DIR            ""                     CACHE PATH   "DXC DLL 根目录")
set(WIN_DXC_ARCH_SUBDIR         ""                     CACHE STRING "DXC 架构子目录（例如 x64/arm64）")
set(WIN_DXC_DLLS                ""                     CACHE STRING "DXC DLL 列表（分号分隔，如 dxcompiler.dll;dxil.dll）")
set(WIN_DXC_TARGET_ALIASES      ""                     CACHE STRING "DXC 拷贝别名目标名（分号分隔）")

set(WIN_AGILITY_ROOT_DIR        ""                     CACHE PATH   "D3D12 Agility SDK 根目录（包含 bin 等）")
set(WIN_AGILITY_ARCH_SUBDIR     ""                     CACHE STRING "Agility 架构子目录（例如 x64/arm64）")
set(WIN_AGILITY_DLLS            ""                     CACHE STRING "Agility DLL 列表（分号分隔，如 D3D12Core.dll;d3d12SDKLayers.dll）")
set(WIN_AGILITY_TARGET_ALIASES  ""                     CACHE STRING "Agility 拷贝别名目标名（分号分隔）")

set(WIN_PIX_ROOT_DIR            ""                     CACHE PATH   "PIX 运行时根目录")
set(WIN_PIX_DLL                 ""                     CACHE FILEPATH "PIX DLL 路径（若提供则复制）")

# 其他附加 DLL（等价 dll）
set(WIN_EXTRA_DLLS              ""                     CACHE STRING "其他需要复制的 DLL（分号分隔，绝对路径）")

# 2) 设定 C++ 标准与输出目录
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 统一设定运行时输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${_win_runtime_dir}")

# 3) 构建主可执行程序（等价 prog）
# 资源文件（.rc）与 MSVC 集成：直接加入 sources 即可参与编译与链接
set(_prog_sources ${WIN_SOURCES} ${WIN_COMMON_SOURCES})
if(WIN_RES_FILE)
    list(APPEND _prog_sources "${WIN_RES_FILE}")
endif()

if(_prog_sources STREQUAL "")
    message(FATAL_ERROR "[win] 未找到任何源文件。请设置 WIN_SOURCES 或在目录中添加 .cpp。")
endif()

add_executable(${WIN_PROG_NAME} ${_prog_sources})

# 4) 可选：构建 wrap 版本（等价 prog_wrap + common_win_wrap）
if(WIN_ENABLE_WRAP)
    set(_wrap_sources ${WIN_SOURCES} ${WIN_COMMON_WRAP_SOURCES})
    if(WIN_RES_WRAP_FILE)
        list(APPEND _wrap_sources "${WIN_RES_WRAP_FILE}")
    endif()
    if(_wrap_sources STREQUAL "")
        message(FATAL_ERROR "[win] WIN_ENABLE_WRAP=ON 但未提供 wrap 源文件。请配置 WIN_COMMON_WRAP_SOURCES 或 WIN_RES_WRAP_FILE。")
    endif()
    add_executable(${WIN_PROG_NAME}_wrap ${_wrap_sources})

    # 将 wrap 的输出也放到相同目录
    set_target_properties(${WIN_PROG_NAME}_wrap PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${_win_runtime_dir}"
    )
endif()

# 5) 统一的拷贝函数（复制 DLL 到目标目录）
function(_win_copy_dlls dll_list_var source_root arch_subdir)
    set(dlls "${${dll_list_var}}")
    if(NOT dlls)
        return()
    endif()
    foreach(d IN LISTS dlls)
        # d 可以是相对（相对 source_root/arch_subdir）或绝对路径
        if(IS_ABSOLUTE "${d}")
            set(_src "${d}")
        else()
            if(source_root AND arch_subdir)
                set(_src "${source_root}/${arch_subdir}/${d}")
            elseif(source_root)
                set(_src "${source_root}/${d}")
            else()
                set(_src "${d}")
            endif()
        endif()

        # 生成目标路径
        get_filename_component(_base "${_src}" NAME)
        set(_dst "${_win_runtime_dir}/${_base}")

        # 以文件为单位创建自定义命令与伪目标，便于单独/批量触发
        set(_copy_target "copy_${_base}")
        add_custom_command(
                OUTPUT "${_dst}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_win_runtime_dir}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_src}" "${_dst}"
                DEPENDS "${_src}"
                COMMENT "[win] Copy ${_base} -> ${_win_runtime_dir}"
                VERBATIM
        )
        add_custom_target(${_copy_target} DEPENDS "${_dst}")

        # 让主程序在构建后可直接拿到 DLL
        add_dependencies(${WIN_PROG_NAME} ${_copy_target})
        if(TARGET ${WIN_PROG_NAME}_wrap)
            add_dependencies(${WIN_PROG_NAME}_wrap ${_copy_target})
        endif()
    endforeach()
endfunction()

# 6) 复制各类 DLL（DXC / Agility / PIX / 额外）
_win_copy_dlls(WIN_DXC_DLLS      "${WIN_DXC_ROOT_DIR}"     "${WIN_DXC_ARCH_SUBDIR}")
_win_copy_dlls(WIN_AGILITY_DLLS  "${WIN_AGILITY_ROOT_DIR}" "${WIN_AGILITY_ARCH_SUBDIR}")

# PIX 单独处理（可能只给出单文件）
if(WIN_PIX_DLL)
    get_filename_component(_pix_name "${WIN_PIX_DLL}" NAME)
    set(_pix_dst "${_win_runtime_dir}/${_pix_name}")
    add_custom_command(
            OUTPUT "${_pix_dst}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_win_runtime_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WIN_PIX_DLL}" "${_pix_dst}"
            DEPENDS "${WIN_PIX_DLL}"
            COMMENT "[win] Copy ${_pix_name} -> ${_win_runtime_dir}"
            VERBATIM
    )
    add_custom_target(copy_${_pix_name} DEPENDS "${_pix_dst}")
    add_dependencies(${WIN_PROG_NAME} copy_${_pix_name})
    if(TARGET ${WIN_PROG_NAME}_wrap)
        add_dependencies(${WIN_PROG_NAME}_wrap copy_${_pix_name})
    endif()
endif()

# 其他额外 DLL（绝对路径列表）
if(WIN_EXTRA_DLLS)
    foreach(_d IN LISTS WIN_EXTRA_DLLS)
        get_filename_component(_n "${_d}" NAME)
        set(_dst "${_win_runtime_dir}/${_n}")
        add_custom_command(
                OUTPUT "${_dst}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_win_runtime_dir}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_d}" "${_dst}"
                DEPENDS "${_d}"
                COMMENT "[win] Copy ${_n} -> ${_win_runtime_dir}"
                VERBATIM
        )
        add_custom_target(copy_${_n} DEPENDS "${_dst}")
        add_dependencies(${WIN_PROG_NAME} copy_${_n})
        if(TARGET ${WIN_PROG_NAME}_wrap)
            add_dependencies(${WIN_PROG_NAME}_wrap copy_${_n})
        endif()
    endforeach()
endif()

# 7) 提供“别名目标”（等价 dxc_target_aliases/agility_target_aliases）
# 用于一次性触发整套 DLL 拷贝，名称由外部决定
function(_win_make_aliases alias_list_var)
    set(_aliases "${${alias_list_var}}")
    if(NOT _aliases)
        return()
    endif()
    foreach(_alias IN LISTS _aliases)
        if(NOT TARGET ${_alias})
            add_custom_target(${_alias})
        endif()
        # 将当前 bin 目录下的所有 copy_* 伪目标作为依赖聚合
        # 说明：这里简单聚合本目录定义的 copy_* 目标（按需可以精确筛选）
        get_property(_allTargets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
        foreach(_t IN LISTS _allTargets)
            string(FIND "${_t}" "copy_" _pos)
            if(_pos EQUAL 0)
                add_dependencies(${_alias} ${_t})
            endif()
        endforeach()
    endforeach()
endfunction()

_win_make_aliases(WIN_DXC_TARGET_ALIASES)
_win_make_aliases(WIN_AGILITY_TARGET_ALIASES)

# 8) 模拟“清理旧产物”的工具目标（近似 arrange_program_clean）
# 用于清理可能的旧命名/后缀残留
add_custom_target(win_arrange_program_clean
        COMMAND ${CMAKE_COMMAND} -E echo "[win] Arranging/Cleaning old program outputs..."
        # 示例：按需删除历史遗留的可执行名/后缀（请根据你的实际命名增改）
        COMMAND ${CMAKE_COMMAND} -E rm -f "${_win_runtime_dir}/${WIN_PROG_NAME}.exe.old"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${_win_runtime_dir}/${WIN_PROG_NAME}_debug.exe"
        VERBATIM
)

# 9) 友好提示
message(STATUS "[win] program: ${WIN_PROG_NAME}")
message(STATUS "[win] wrap: ${WIN_ENABLE_WRAP}")
message(STATUS "[win] out: ${_win_runtime_dir}")