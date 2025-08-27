# ... existing code ...
file(GLOB LOCAL_SOURCES "*.cpp")

# 直接把这些源附加到可执行目标 godot
target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
)
# ... existing code ...
# ===== 选项与变量（可根据需要修改/传入） =====
option(BROTLI "Enable brotli support toggle" ON)
option(BUILTIN_BROTLI "Use builtin brotli" ON)
option(BUILTIN_CLIPPER2 "Use builtin clipper2" ON)
option(BUILTIN_ZLIB "Use builtin zlib" ON)
option(DEV_BUILD "Developer build (like env.dev_build)" OFF)
option(BUILTIN_ZSTD "Use builtin zstd" ON)
option(BUILTIN_CERTS "Use builtin certs" ON)

set(SYSTEM_CERTS_PATH "" CACHE STRING "Path to system certificates for SSL")
set(DISABLED_CLASSES "" CACHE STRING "Semicolon-separated list of disabled classes")

# 版本信息（可通过 -D 覆盖）
set(VERSION_SHORT_NAME "godot" CACHE STRING "")
set(VERSION_NAME "Godot Engine" CACHE STRING "")
set(VERSION_MAJOR 4 CACHE STRING "")
set(VERSION_MINOR 0 CACHE STRING "")
set(VERSION_PATCH 0 CACHE STRING "")
set(VERSION_STATUS "stable" CACHE STRING "")
set(VERSION_BUILD "" CACHE STRING "")
set(VERSION_MODULE_CONFIG "" CACHE STRING "")
set(VERSION_WEBSITE "https://godotengine.org" CACHE STRING "")
set(VERSION_DOCS_BRANCH "stable" CACHE STRING "")

# 工程根与构建根作为包含目录（用于访问生成头文件）
target_include_directories(godot PRIVATE
        ${CMAKE_BINARY_DIR}
)

# ===== 第三方依赖源注入 =====
# thirdparty/misc
set(THIRDPARTY_MISC_DIR ${CMAKE_SOURCE_DIR}/thirdparty/misc)
set(THIRDPARTY_MISC_SOURCES
        ${THIRDPARTY_MISC_DIR}/fastlz.c
        ${THIRDPARTY_MISC_DIR}/r128.c
        ${THIRDPARTY_MISC_DIR}/smaz.c
        ${THIRDPARTY_MISC_DIR}/pcg.cpp
        ${THIRDPARTY_MISC_DIR}/polypartition.cpp
        ${THIRDPARTY_MISC_DIR}/smolv.cpp
)
target_sources(godot PRIVATE ${THIRDPARTY_MISC_SOURCES})

# brotli
if(BROTLI AND BUILTIN_BROTLI)
    set(BROTLI_DIR ${CMAKE_SOURCE_DIR}/thirdparty/brotli)
    set(BROTLI_SOURCES
            ${BROTLI_DIR}/common/constants.c
            ${BROTLI_DIR}/common/context.c
            ${BROTLI_DIR}/common/dictionary.c
            ${BROTLI_DIR}/common/platform.c
            ${BROTLI_DIR}/common/shared_dictionary.c
            ${BROTLI_DIR}/common/transform.c
            ${BROTLI_DIR}/dec/bit_reader.c
            ${BROTLI_DIR}/dec/decode.c
            ${BROTLI_DIR}/dec/huffman.c
            ${BROTLI_DIR}/dec/state.c
    )
    target_sources(godot PRIVATE ${BROTLI_SOURCES})
    target_include_directories(godot PRIVATE ${BROTLI_DIR}/include)

    # 与 SCons 中的 sanitizer 检查类似（如需更精确可引入工具链变量）
    if(CMAKE_BUILD_TYPE MATCHES "ASAN|TSAN|UBSAN|MSAN")
        target_compile_definitions(godot PRIVATE BROTLI_BUILD_PORTABLE)
    endif()
endif()

# clipper2
if(BUILTIN_CLIPPER2)
    set(CLIPPER_DIR ${CMAKE_SOURCE_DIR}/thirdparty/clipper2)
    set(CLIPPER_SOURCES
            ${CLIPPER_DIR}/src/clipper.engine.cpp
            ${CLIPPER_DIR}/src/clipper.offset.cpp
            ${CLIPPER_DIR}/src/clipper.rectclip.cpp
    )
    target_sources(godot PRIVATE ${CLIPPER_SOURCES})
    target_include_directories(godot PRIVATE ${CLIPPER_DIR}/include)
    target_compile_definitions(godot PRIVATE CLIPPER2_ENABLED)
endif()

# zlib
if(BUILTIN_ZLIB)
    set(ZLIB_DIR ${CMAKE_SOURCE_DIR}/thirdparty/zlib)
    set(ZLIB_SOURCES
            ${ZLIB_DIR}/adler32.c
            ${ZLIB_DIR}/compress.c
            ${ZLIB_DIR}/crc32.c
            ${ZLIB_DIR}/deflate.c
            ${ZLIB_DIR}/inffast.c
            ${ZLIB_DIR}/inflate.c
            ${ZLIB_DIR}/inftrees.c
            ${ZLIB_DIR}/trees.c
            ${ZLIB_DIR}/uncompr.c
            ${ZLIB_DIR}/zutil.c
    )
    target_sources(godot PRIVATE ${ZLIB_SOURCES})
    target_include_directories(godot PRIVATE ${ZLIB_DIR})
    if(DEV_BUILD)
        target_compile_definitions(godot PRIVATE ZLIB_DEBUG)
    endif()
endif()

# minizip（始终内置）
set(MINIZIP_DIR ${CMAKE_SOURCE_DIR}/thirdparty/minizip)
set(MINIZIP_SOURCES
        ${MINIZIP_DIR}/ioapi.c
        ${MINIZIP_DIR}/unzip.c
        ${MINIZIP_DIR}/zip.c
)
target_sources(godot PRIVATE ${MINIZIP_SOURCES})

# zstd
if(BUILTIN_ZSTD)
    set(ZSTD_DIR ${CMAKE_SOURCE_DIR}/thirdparty/zstd)
    set(ZSTD_SOURCES
            ${ZSTD_DIR}/common/debug.c
            ${ZSTD_DIR}/common/entropy_common.c
            ${ZSTD_DIR}/common/error_private.c
            ${ZSTD_DIR}/common/fse_decompress.c
            ${ZSTD_DIR}/common/pool.c
            ${ZSTD_DIR}/common/threading.c
            ${ZSTD_DIR}/common/xxhash.c
            ${ZSTD_DIR}/common/zstd_common.c
            ${ZSTD_DIR}/compress/fse_compress.c
            ${ZSTD_DIR}/compress/hist.c
            ${ZSTD_DIR}/compress/huf_compress.c
            ${ZSTD_DIR}/compress/zstd_compress.c
            ${ZSTD_DIR}/compress/zstd_double_fast.c
            ${ZSTD_DIR}/compress/zstd_fast.c
            ${ZSTD_DIR}/compress/zstd_lazy.c
            ${ZSTD_DIR}/compress/zstd_ldm.c
            ${ZSTD_DIR}/compress/zstd_opt.c
            ${ZSTD_DIR}/compress/zstdmt_compress.c
            ${ZSTD_DIR}/compress/zstd_compress_literals.c
            ${ZSTD_DIR}/compress/zstd_compress_sequences.c
            ${ZSTD_DIR}/compress/zstd_compress_superblock.c
            ${ZSTD_DIR}/decompress/huf_decompress.c
            ${ZSTD_DIR}/decompress/zstd_ddict.c
            ${ZSTD_DIR}/decompress/zstd_decompress_block.c
            ${ZSTD_DIR}/decompress/zstd_decompress.c
    )

    # 按平台/架构追加 AMD64 汇编文件
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        if(CMAKE_SYSTEM_NAME MATCHES "Android|Linux|Darwin|iOS")
            list(APPEND ZSTD_SOURCES ${ZSTD_DIR}/decompress/huf_decompress_amd64.S)
        endif()
    endif()

    target_sources(godot PRIVATE ${ZSTD_SOURCES})
    target_include_directories(godot PRIVATE ${ZSTD_DIR} ${ZSTD_DIR}/common)
    target_compile_definitions(godot PRIVATE ZSTD_STATIC_LINKING_ONLY)
endif()

# ===== 生成 disabled_classes.gen.h =====
# 由 CMake 变量 DISABLED_CLASSES（以分号分隔）生成
set(DISABLED_CLASSES_HEADER ${CMAKE_BINARY_DIR}/disabled_classes.gen.h)
file(GENERATE OUTPUT ${DISABLED_CLASSES_HEADER} CONTENT
        "/* Auto-generated */\n$<$<BOOL:${DISABLED_CLASSES}>:$<JOIN:$<SPLIT:${DISABLED_CLASSES},;>,\n>>\n"
)
# 将上面的简单拼接改为真正的 #define 列表
# 由于 file(GENERATE)不易做循环，使用 configure 阶段生成
unset(_dc_lines)
foreach(_dc ${DISABLED_CLASSES})
    string(APPEND _dc_lines "#define ClassDB_Disable_${_dc} 1\n")
endforeach()
file(WRITE ${DISABLED_CLASSES_HEADER} "/* Auto-generated */\n${_dc_lines}")

target_sources(godot PRIVATE ${DISABLED_CLASSES_HEADER})

# ===== 生成 version_generated.gen.h =====
set(VERSION_HEADER ${CMAKE_BINARY_DIR}/version_generated.gen.h)
file(WRITE ${VERSION_HEADER}
        "#ifndef GODOT_VERSION_GENERATED_GEN_H\n#define GODOT_VERSION_GENERATED_GEN_H\n"
        "#define VERSION_SHORT_NAME \"${VERSION_SHORT_NAME}\"\n"
        "#define VERSION_NAME \"${VERSION_NAME}\"\n"
        "#define VERSION_MAJOR ${VERSION_MAJOR}\n"
        "#define VERSION_MINOR ${VERSION_MINOR}\n"
        "#define VERSION_PATCH ${VERSION_PATCH}\n"
        "#define VERSION_STATUS \"${VERSION_STATUS}\"\n"
        "#define VERSION_BUILD \"${VERSION_BUILD}\"\n"
        "#define VERSION_MODULE_CONFIG \"${VERSION_MODULE_CONFIG}\"\n"
        "#define VERSION_WEBSITE \"${VERSION_WEBSITE}\"\n"
        "#define VERSION_DOCS_BRANCH \"${VERSION_DOCS_BRANCH}\"\n"
        "#define VERSION_DOCS_URL \"https://docs.godotengine.org/en/\" VERSION_DOCS_BRANCH\n"
        "#endif\n"
)
target_sources(godot PRIVATE ${VERSION_HEADER})

# ===== 生成 version_hash.gen.cpp =====
# 从 git 获取哈希与时间戳
find_package(Git QUIET)
set(_GIT_HASH "unknown")
set(_GIT_TIMESTAMP 0)
if(GIT_FOUND)
    execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE _GIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )
    execute_process(
            COMMAND ${GIT_EXECUTABLE} log -1 --format=%ct
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE _GIT_TIMESTAMP
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )
endif()

set(VERSION_HASH_CPP ${CMAKE_BINARY_DIR}/version_hash.gen.cpp)
file(WRITE ${VERSION_HASH_CPP}
        "#include \"core/version.h\"\n\n"
        "const char *const VERSION_HASH = \"${_GIT_HASH}\";\n"
        "const uint64_t VERSION_TIMESTAMP = ${_GIT_TIMESTAMP};\n"
)
target_sources(godot PRIVATE ${VERSION_HASH_CPP})

# ===== 生成 script_encryption_key.gen.cpp =====
# 从环境变量 SCRIPT_AES256_ENCRYPTION_KEY 读取并校验（64 hex）
set(_RAW_KEY "$ENV{SCRIPT_AES256_ENCRYPTION_KEY}")
if(_RAW_KEY STREQUAL "")
    string(REPEAT "0" 64 _RAW_KEY)  # 默认全 0
endif()

string(LENGTH "${_RAW_KEY}" _KEY_LEN)
if(NOT _KEY_LEN EQUAL 64)
    message(FATAL_ERROR "Invalid AES256 encryption key length: ${_KEY_LEN}, expect 64 hex chars.")
endif()
if(NOT _RAW_KEY MATCHES "^[0-9a-fA-F]+$")
    message(FATAL_ERROR "Invalid AES256 encryption key, must be hex string.")
endif()

# 转换为 32 个十进制字节
set(_KEY_BYTES "")
math(EXPR _N "64 / 2")
foreach(i RANGE 0 31)
    math(EXPR pos "i*2")
    string(SUBSTRING "${_RAW_KEY}" ${pos} 2 _pair)
    string(TOUPPER "${_pair}" _pairu)
    # 将 2 位十六进制转为十进制
    # CMake 没有直接的 16 进制转十进制内建，只能拆分
    string(SUBSTRING "${_pairu}" 0 1 _h1)
    string(SUBSTRING "${_pairu}" 1 1 _h2)
    set(_digits "0123456789ABCDEF")
    string(FIND "${_digits}" "${_h1}" _v1)
    string(FIND "${_digits}" "${_h2}" _v2)
    if(_v1 LESS 0 OR _v2 LESS 0)
        message(FATAL_ERROR "Invalid hex pair: ${_pair}")
    endif()
    math(EXPR _byte "(_v1*16)+_v2")
    if(i EQUAL 0)
        set(_KEY_BYTES "${_byte}")
    else()
        set(_KEY_BYTES "${_KEY_BYTES}, ${_byte}")
    endif()
endforeach()

set(ENCRYPT_CPP ${CMAKE_BINARY_DIR}/script_encryption_key.gen.cpp)
file(WRITE ${ENCRYPT_CPP}
        "#include \"core/config/project_settings.h\"\n\n"
        "uint8_t script_encryption_key[32] = {\n\t${_KEY_BYTES}\n};\n"
)
target_sources(godot PRIVATE ${ENCRYPT_CPP})

# ===== 生成占位头文件（如需真实内容请改为你的生成脚本） =====
# core/io/certs_compressed.gen.h
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/core/io)
file(WRITE ${CMAKE_BINARY_DIR}/core/io/certs_compressed.gen.h
        "/* TODO: generate real certs header from ${CMAKE_SOURCE_DIR}/thirdparty/certs/ca-certificates.crt\n"
        "   BUILTIN_CERTS=${BUILTIN_CERTS}, SYSTEM_CERTS_PATH=${SYSTEM_CERTS_PATH} */\n"
)

# core/authors.gen.h
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/core)
file(WRITE ${CMAKE_BINARY_DIR}/core/authors.gen.h
        "/* TODO: generate real authors header from ${CMAKE_SOURCE_DIR}/AUTHORS.md */\n"
)

# core/donors.gen.h
file(WRITE ${CMAKE_BINARY_DIR}/core/donors.gen.h
        "/* TODO: generate real donors header from ${CMAKE_SOURCE_DIR}/DONORS.md */\n"
)

# core/license.gen.h
file(WRITE ${CMAKE_BINARY_DIR}/core/license.gen.h
        "/* TODO: generate real license header from ${CMAKE_SOURCE_DIR}/COPYRIGHT.txt and LICENSE.txt */\n"
)

# 保证包含路径可找到上述生成文件
target_include_directories(godot PRIVATE ${CMAKE_BINARY_DIR})

add_subdirectory(config)
add_subdirectory(crypto)
add_subdirectory(debugger)
add_subdirectory(error)
add_subdirectory(extension)
add_subdirectory(input)
add_subdirectory(io)
add_subdirectory(math)
add_subdirectory(variant)
add_subdirectory(object)
add_subdirectory(templates)
add_subdirectory(string)
add_subdirectory(os)

