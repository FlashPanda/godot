# 子级 CMakeLists：复现 SCsub 的行为，不声明 project()

cmake_minimum_required(VERSION 3.16)

# 确保上层已定义 godot 目标
if (NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Define it in the parent CMakeLists.txt before add_subdirectory.")
endif()

# 选项：是否内置构建 thirdparty glslang（对应 env['builtin_glslang']）
option(GODOT_BUILTIN_GLSLANG "Build thirdparty glslang with the module" ON)

# 1) 将本目录下 *.cpp 并入 godot
file(GLOB CONFIGURE_DEPENDS LOCAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
if (LOCAL_SOURCES STREQUAL "")
    message(STATUS "[glslang module] No local .cpp found in ${CMAKE_CURRENT_SOURCE_DIR}")
endif()
target_sources(godot PRIVATE ${LOCAL_SOURCES})

# 2) 内置 glslang：收集 thirdparty 源并以 OBJECT 库方式构建，附加到 godot
if (GODOT_BUILTIN_GLSLANG)
    set(_GLSLANG_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/glslang")

    # 与 SCsub 中列出的 thirdparty 源保持一致
    set(_glslang_rel_sources
            "glslang/GenericCodeGen/CodeGen.cpp"
            "glslang/GenericCodeGen/Link.cpp"
            "glslang/MachineIndependent/attribute.cpp"
            "glslang/MachineIndependent/Constant.cpp"
            "glslang/MachineIndependent/glslang_tab.cpp"
            "glslang/MachineIndependent/InfoSink.cpp"
            "glslang/MachineIndependent/Initialize.cpp"
            "glslang/MachineIndependent/Intermediate.cpp"
            "glslang/MachineIndependent/intermOut.cpp"
            "glslang/MachineIndependent/IntermTraverse.cpp"
            "glslang/MachineIndependent/iomapper.cpp"
            "glslang/MachineIndependent/limits.cpp"
            "glslang/MachineIndependent/linkValidate.cpp"
            "glslang/MachineIndependent/parseConst.cpp"
            "glslang/MachineIndependent/ParseContextBase.cpp"
            "glslang/MachineIndependent/ParseHelper.cpp"
            "glslang/MachineIndependent/PoolAlloc.cpp"
            "glslang/MachineIndependent/preprocessor/PpAtom.cpp"
            "glslang/MachineIndependent/preprocessor/PpContext.cpp"
            "glslang/MachineIndependent/preprocessor/Pp.cpp"
            "glslang/MachineIndependent/preprocessor/PpScanner.cpp"
            "glslang/MachineIndependent/preprocessor/PpTokens.cpp"
            "glslang/MachineIndependent/propagateNoContraction.cpp"
            "glslang/MachineIndependent/reflection.cpp"
            "glslang/MachineIndependent/RemoveTree.cpp"
            "glslang/MachineIndependent/Scan.cpp"
            "glslang/MachineIndependent/ShaderLang.cpp"
            "glslang/MachineIndependent/SpirvIntrinsics.cpp"
            "glslang/MachineIndependent/SymbolTable.cpp"
            "glslang/MachineIndependent/Versions.cpp"
            "glslang/ResourceLimits/ResourceLimits.cpp"
            "SPIRV/disassemble.cpp"
            "SPIRV/doc.cpp"
            "SPIRV/GlslangToSpv.cpp"
            "SPIRV/InReadableOrder.cpp"
            "SPIRV/Logger.cpp"
            "SPIRV/SpvBuilder.cpp"
            "SPIRV/SpvPostProcess.cpp"
            "SPIRV/SPVRemapper.cpp"
            "SPIRV/SpvTools.cpp"
    )

    # 平台相关的 ossource.cpp
    if (WIN32)
        list(APPEND _glslang_rel_sources "glslang/OSDependent/Windows/ossource.cpp")
    else()
        list(APPEND _glslang_rel_sources "glslang/OSDependent/Unix/ossource.cpp")
    endif()

    # 补全为绝对路径
    set(_glslang_sources "")
    foreach(_rel IN LISTS _glslang_rel_sources)
        list(APPEND _glslang_sources "${_GLSLANG_ROOT}/${_rel}")
    endforeach()

    # 使用 OBJECT 库承载编译选项/定义（之后把对象加入 godot）
    add_library(glslang_thirdparty OBJECT ${_glslang_sources})

    # 对齐 SCsub：第三方代码定义 ENABLE_OPT=0，且关闭警告
    target_compile_definitions(glslang_thirdparty PRIVATE ENABLE_OPT=0)

    if (MSVC)
        target_compile_options(glslang_thirdparty PRIVATE /w)
        # MSVC 下用常规包含目录
        target_include_directories(godot PRIVATE
                "${_GLSLANG_ROOT}"
                "${CMAKE_SOURCE_DIR}/thirdparty"
        )
    else()
        target_compile_options(glslang_thirdparty PRIVATE -w)
        # 非 MSVC：把第三方包含目录作为 SYSTEM 抑制头文件警告
        target_include_directories(godot SYSTEM PRIVATE
                "${_GLSLANG_ROOT}"
                "${CMAKE_SOURCE_DIR}/thirdparty"
        )
    endif()

    # 将第三方对象文件并入 godot，并建立依赖关系
    target_sources(godot PRIVATE $<TARGET_OBJECTS:glslang_thirdparty>)
    add_dependencies(godot glslang_thirdparty)
endif()