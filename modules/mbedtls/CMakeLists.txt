# 最低版本可按工程统一要求调整
cmake_minimum_required(VERSION 3.20)

# 选项对应 SCons 环境中的开关，按需在顶层工程覆盖
option(GODOT_BUILTIN_MBEDTLS "Build with bundled thirdparty/mbedtls" ON)  # 对应 env['builtin_mbedtls']
option(GODOT_TESTS "Enable tests for this module" OFF)                     # 对应 env['tests']
option(GODOT_DISABLE_EXCEPTIONS "Disable exceptions (doctest compat macro)" OFF) # 对应 env['disable_exceptions']

# 计算在不同生成器/编译器下 MBEDTLS_CONFIG_FILE 宏的取值形式
# SCsub 中在 msvc+ninja 下使用 <...>，其余使用 "..."
set(_MBEDTLS_CONFIG_HEADER_PATH "thirdparty/mbedtls/include/godot_module_mbedtls_config.h")
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_GENERATOR STREQUAL "Ninja")
    set(_MBEDTLS_CONFIG_DEFINE "<${_MBEDTLS_CONFIG_HEADER_PATH}>")
else()
    set(_MBEDTLS_CONFIG_DEFINE "\"${_MBEDTLS_CONFIG_HEADER_PATH}\"")
endif()

# ---------------------------
# 1) thirdparty/mbedtls 源码（可选，取决于 GODOT_BUILTIN_MBEDTLS）
# ---------------------------
set(_MBEDTLS_TP_SOURCES)
if (GODOT_BUILTIN_MBEDTLS)
    # 与 SCsub 中相同的源文件清单（位于 thirdparty/mbedtls/library/）
    set(_MBEDTLS_TP_SOURCES
            thirdparty/mbedtls/library/aes.c
            thirdparty/mbedtls/library/aesce.c
            thirdparty/mbedtls/library/aesni.c
            thirdparty/mbedtls/library/aria.c
            thirdparty/mbedtls/library/asn1parse.c
            thirdparty/mbedtls/library/asn1write.c
            thirdparty/mbedtls/library/base64.c
            thirdparty/mbedtls/library/bignum.c
            thirdparty/mbedtls/library/bignum_core.c
            thirdparty/mbedtls/library/bignum_mod_raw.c
            thirdparty/mbedtls/library/camellia.c
            thirdparty/mbedtls/library/ccm.c
            thirdparty/mbedtls/library/chacha20.c
            thirdparty/mbedtls/library/chachapoly.c
            thirdparty/mbedtls/library/cipher.c
            thirdparty/mbedtls/library/cipher_wrap.c
            thirdparty/mbedtls/library/cmac.c
            thirdparty/mbedtls/library/constant_time.c
            thirdparty/mbedtls/library/ctr_drbg.c
            thirdparty/mbedtls/library/debug.c
            thirdparty/mbedtls/library/des.c
            thirdparty/mbedtls/library/dhm.c
            thirdparty/mbedtls/library/ecdh.c
            thirdparty/mbedtls/library/ecdsa.c
            thirdparty/mbedtls/library/ecjpake.c
            thirdparty/mbedtls/library/ecp.c
            thirdparty/mbedtls/library/ecp_curves.c
            thirdparty/mbedtls/library/entropy.c
            thirdparty/mbedtls/library/entropy_poll.c
            thirdparty/mbedtls/library/error.c
            thirdparty/mbedtls/library/gcm.c
            thirdparty/mbedtls/library/hkdf.c
            thirdparty/mbedtls/library/hmac_drbg.c
            thirdparty/mbedtls/library/md.c
            thirdparty/mbedtls/library/md5.c
            thirdparty/mbedtls/library/memory_buffer_alloc.c
            thirdparty/mbedtls/library/mps_reader.c
            thirdparty/mbedtls/library/mps_trace.c
            thirdparty/mbedtls/library/net_sockets.c
            thirdparty/mbedtls/library/nist_kw.c
            thirdparty/mbedtls/library/oid.c
            thirdparty/mbedtls/library/padlock.c
            thirdparty/mbedtls/library/pem.c
            thirdparty/mbedtls/library/pk.c
            thirdparty/mbedtls/library/pk_ecc.c
            thirdparty/mbedtls/library/pk_wrap.c
            thirdparty/mbedtls/library/pkcs12.c
            thirdparty/mbedtls/library/pkcs5.c
            thirdparty/mbedtls/library/pkcs7.c
            thirdparty/mbedtls/library/pkparse.c
            thirdparty/mbedtls/library/pkwrite.c
            thirdparty/mbedtls/library/platform.c
            thirdparty/mbedtls/library/platform_util.c
            thirdparty/mbedtls/library/poly1305.c
            thirdparty/mbedtls/library/psa_crypto.c
            thirdparty/mbedtls/library/psa_crypto_aead.c
            thirdparty/mbedtls/library/psa_crypto_cipher.c
            thirdparty/mbedtls/library/psa_crypto_client.c
            thirdparty/mbedtls/library/psa_crypto_driver_wrappers_no_static.c
            thirdparty/mbedtls/library/psa_crypto_ecp.c
            thirdparty/mbedtls/library/psa_crypto_ffdh.c
            thirdparty/mbedtls/library/psa_crypto_hash.c
            thirdparty/mbedtls/library/psa_crypto_mac.c
            thirdparty/mbedtls/library/psa_crypto_pake.c
            thirdparty/mbedtls/library/psa_crypto_rsa.c
            thirdparty/mbedtls/library/psa_crypto_se.c
            thirdparty/mbedtls/library/psa_crypto_slot_management.c
            thirdparty/mbedtls/library/psa_crypto_storage.c
            thirdparty/mbedtls/library/psa_its_file.c
            thirdparty/mbedtls/library/psa_util.c
            thirdparty/mbedtls/library/ripemd160.c
            thirdparty/mbedtls/library/rsa.c
            thirdparty/mbedtls/library/rsa_alt_helpers.c
            thirdparty/mbedtls/library/sha1.c
            thirdparty/mbedtls/library/sha3.c
            thirdparty/mbedtls/library/sha256.c
            thirdparty/mbedtls/library/sha512.c
            thirdparty/mbedtls/library/ssl_cache.c
            thirdparty/mbedtls/library/ssl_ciphersuites.c
            thirdparty/mbedtls/library/ssl_client.c
            thirdparty/mbedtls/library/ssl_cookie.c
            thirdparty/mbedtls/library/ssl_debug_helpers_generated.c
            thirdparty/mbedtls/library/ssl_msg.c
            thirdparty/mbedtls/library/ssl_ticket.c
            thirdparty/mbedtls/library/ssl_tls.c
            thirdparty/mbedtls/library/ssl_tls12_client.c
            thirdparty/mbedtls/library/ssl_tls12_server.c
            thirdparty/mbedtls/library/ssl_tls13_client.c
            thirdparty/mbedtls/library/ssl_tls13_generic.c
            thirdparty/mbedtls/library/ssl_tls13_keys.c
            thirdparty/mbedtls/library/ssl_tls13_server.c
            thirdparty/mbedtls/library/threading.c
            thirdparty/mbedtls/library/timing.c
            thirdparty/mbedtls/library/version.c
            thirdparty/mbedtls/library/version_features.c
            thirdparty/mbedtls/library/x509.c
            thirdparty/mbedtls/library/x509_create.c
            thirdparty/mbedtls/library/x509_crl.c
            thirdparty/mbedtls/library/x509_crt.c
            thirdparty/mbedtls/library/x509_csr.c
            thirdparty/mbedtls/library/x509write.c
            thirdparty/mbedtls/library/x509write_crt.c
            thirdparty/mbedtls/library/x509write_csr.c
    )

    # 作为 OBJECT 库构建 thirdparty 源
    add_library(mbedtls_thirdparty OBJECT ${_MBEDTLS_TP_SOURCES})

    # thirdparty 关闭警告，匹配 SCsub 的 disable_warnings()
    if (MSVC)
        target_compile_options(mbedtls_thirdparty PRIVATE /w)
    else()
        target_compile_options(mbedtls_thirdparty PRIVATE -w)
    endif()

    # thirdparty 的包含目录与宏
    target_include_directories(mbedtls_thirdparty PRIVATE thirdparty/mbedtls/include)
    target_compile_definitions(mbedtls_thirdparty PRIVATE MBEDTLS_CONFIG_FILE=${_MBEDTLS_CONFIG_DEFINE})

    # 将配置头文件显式加入依赖，确保其变化触发重编译
    target_sources(mbedtls_thirdparty PRIVATE ${_MBEDTLS_CONFIG_HEADER_PATH})
endif()

# ---------------------------
# 2) 模块自身源码（本目录 *.cpp + 可选 tests/*.cpp）
# ---------------------------
# 收集模块源码
file(GLOB _MBEDTLS_MODULE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

# 测试开关
if (GODOT_TESTS)
    file(GLOB _MBEDTLS_TEST_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_LIST_DIR}/tests/*.cpp")
    list(APPEND _MBEDTLS_MODULE_SOURCES ${_MBEDTLS_TEST_SOURCES})
endif()

# 模块也构建为 OBJECT 库，供上层聚合
add_library(mbedtls_module OBJECT ${_MBEDTLS_MODULE_SOURCES})

# 若启用内置 thirdparty，则模块同样需要包含目录与同一宏
if (GODOT_BUILTIN_MBEDTLS)
    target_include_directories(mbedtls_module PRIVATE thirdparty/mbedtls/include)
    target_compile_definitions(mbedtls_module PRIVATE MBEDTLS_CONFIG_FILE=${_MBEDTLS_CONFIG_DEFINE})
    # 与 SCsub 等价：当 thirdparty 更新时，强制模块重编译
    add_dependencies(mbedtls_module mbedtls_thirdparty)
    target_sources(mbedtls_module PRIVATE ${_MBEDTLS_CONFIG_HEADER_PATH})
endif()

# 测试宏
if (GODOT_TESTS)
    target_compile_definitions(mbedtls_module PRIVATE TESTS_ENABLED)
    if (GODOT_DISABLE_EXCEPTIONS)
        target_compile_definitions(mbedtls_module PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
    endif()
endif()

# ---------------------------
# 3) 向父作用域导出对象，便于在更高层 add_library 聚合
# ---------------------------
set(MODULE_MBEDTLS_OBJECTS
        $<TARGET_OBJECTS:mbedtls_module>
        PARENT_SCOPE)

if (GODOT_BUILTIN_MBEDTLS)
    # 父层可以直接把对象加入到统一静态库/目标里
    set(MODULE_MBEDTLS_OBJECTS
            $<TARGET_OBJECTS:mbedtls_thirdparty>
            $<TARGET_OBJECTS:mbedtls_module>
            PARENT_SCOPE)
endif()