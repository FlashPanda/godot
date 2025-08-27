# 要求：父级最终使用这些变量生成目标
# - GODOT_DRIVERS_SOURCES
# - GODOT_INCLUDE_DIRS
# - GODOT_COMPILE_DEFINITIONS
#
# 可配置项：
# - BUILTIN_LIBPNG: ON/OFF，是否使用内置 thirdparty/libpng 源构建

# 确保父作用域列表存在
if(NOT DEFINED GODOT_DRIVERS_SOURCES)
    set(GODOT_DRIVERS_SOURCES "" CACHE INTERNAL "" FORCE)
endif()
if(NOT DEFINED GODOT_INCLUDE_DIRS)
    set(GODOT_INCLUDE_DIRS "" CACHE INTERNAL "" FORCE)
endif()
if(NOT DEFINED GODOT_COMPILE_DEFINITIONS)
    set(GODOT_COMPILE_DEFINITIONS "" CACHE INTERNAL "" FORCE)
endif()

# 收集当前目录下驱动源码（等价于 add_source_files(driver_obj, "*.cpp")）
file(GLOB DRIVER_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# thirdparty/libpng 处理
set(THIRDPARTY_SOURCES_ALL "")
if(BUILTIN_LIBPNG)
    set(thirdparty_dir "${PROJECT_SOURCE_DIR}/thirdparty/libpng")

    set(thirdparty_sources
            "${thirdparty_dir}/png.c"
            "${thirdparty_dir}/pngerror.c"
            "${thirdparty_dir}/pngget.c"
            "${thirdparty_dir}/pngmem.c"
            "${thirdparty_dir}/pngpread.c"
            "${thirdparty_dir}/pngread.c"
            "${thirdparty_dir}/pngrio.c"
            "${thirdparty_dir}/pngrtran.c"
            "${thirdparty_dir}/pngrutil.c"
            "${thirdparty_dir}/pngset.c"
            "${thirdparty_dir}/pngtrans.c"
            "${thirdparty_dir}/pngwio.c"
            "${thirdparty_dir}/pngwrite.c"
            "${thirdparty_dir}/pngwtran.c"
            "${thirdparty_dir}/pngwutil.c"
    )

    # 包含目录（SCons: env_png.Prepend + env.Prepend）
    list(APPEND GODOT_INCLUDE_DIRS "${thirdparty_dir}")

    # 架构特定源码与定义
    set(arch "${CMAKE_SYSTEM_PROCESSOR}")

    if(arch MATCHES "^(arm|aarch64)")
        if(MSVC)
            # SCons: MSVC 下禁用 NEON
            list(APPEND GODOT_COMPILE_DEFINITIONS "PNG_ARM_NEON_OPT=0")
        else()
            list(APPEND thirdparty_sources
                    "${thirdparty_dir}/arm/arm_init.c"
                    "${thirdparty_dir}/arm/filter_neon_intrinsics.c"
                    "${thirdparty_dir}/arm/palette_neon_intrinsics.c"
            )
        endif()
    elseif(arch MATCHES "^(x86_64|AMD64|i[3-6]86)")
        list(APPEND GODOT_COMPILE_DEFINITIONS "PNG_INTEL_SSE")
        list(APPEND thirdparty_sources
                "${thirdparty_dir}/intel/intel_init.c"
                "${thirdparty_dir}/intel/filter_sse2_intrinsics.c"
        )
    elseif(arch MATCHES "((powerpc|ppc)64)")
        list(APPEND thirdparty_sources
                "${thirdparty_dir}/powerpc/powerpc_init.c"
                "${thirdparty_dir}/powerpc/filter_vsx_intrinsics.c"
        )
    endif()

    # 第三方源码禁用告警（SCons: env_thirdparty.disable_warnings()）
    set(_no_warn_flag "")
    if(MSVC)
        set(_no_warn_flag "/w")
    else()
        set(_no_warn_flag "-w")
    endif()
    if(_no_warn_flag)
        set_source_files_properties(${thirdparty_sources}
                PROPERTIES COMPILE_OPTIONS "${_no_warn_flag}"
        )
    endif()

    list(APPEND THIRDPARTY_SOURCES_ALL ${thirdparty_sources})
endif()

# 将 thirdparty 源与本驱动目录下源码统一追加进父作用域集合
set(GODOT_DRIVERS_SOURCES
        ${GODOT_DRIVERS_SOURCES}
        ${THIRDPARTY_SOURCES_ALL}
        ${DRIVER_LOCAL_SOURCES}
        PARENT_SCOPE
)

# 将包含目录与编译定义传递给父作用域
list(REMOVE_DUPLICATES GODOT_INCLUDE_DIRS)
set(GODOT_INCLUDE_DIRS "${GODOT_INCLUDE_DIRS}" PARENT_SCOPE)

list(REMOVE_DUPLICATES GODOT_COMPILE_DEFINITIONS)
set(GODOT_COMPILE_DEFINITIONS "${GODOT_COMPILE_DEFINITIONS}" PARENT_SCOPE)