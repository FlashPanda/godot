# 子目录 CMakeLists：复刻 modules/text_server_adv/SCsub 的行为
# - 不声明 project()，仅作为父工程的子目录被包含
# - 将本模块源码与所需 thirdparty（可选）一并编译并注入父目标 `godot`
# - 尽可能对齐 SCons 里的条件宏、包含目录与平台差异

cmake_minimum_required(VERSION 3.16)

# 0) 前置：确保父级已定义目标 `godot`（可执行或库）
if (NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Define it in the parent CMakeLists.txt before add_subdirectory.")
endif()

# 1) 选项开关（映射 SCsub 中的 env[...] 与模块开关）
option(MODULE_SVG_ENABLED         "Enable svg module cooperation (thorvg headers/defines)"            ON)  # 对齐：if 'svg' in env.module_list
option(MODULE_FREETYPE_ENABLED    "Enable freetype-dependent parts in this module"                    ON)  # 对齐：'freetype' in env.module_list
option(MODULE_MSDFGEN_ENABLED     "Enable msdfgen-dependent parts in this module"                     ON)  # 对齐：'msdfgen' in env.module_list
option(MODULE_GRAPHITE_ENABLED    "Enable graphite integration when freetype is present"              OFF) # 对齐：env['graphite']

option(GODOT_BUILTIN_HARFBUZZ     "Build bundled thirdparty harfbuzz"                                 ON)  # 对齐：env['builtin_harfbuzz']
option(GODOT_BUILTIN_GRAPHITE2    "Build bundled thirdparty graphite2 (requires freetype+graphite)"   ON)  # 对齐：env['builtin_graphite']
option(GODOT_BUILTIN_FREETYPE     "Use bundled thirdparty freetype headers"                           ON)  # 对齐：env['builtin_freetype']
option(GODOT_BUILTIN_ICU4C        "Build bundled thirdparty ICU"                                      ON)  # 对齐：env['builtin_icu4c']

# Godot 编辑器构建（影响 ICU 静态数据是否生成/使用）
option(GODOT_EDITOR_BUILD         "Build with editor features (ICU static data, etc.)"                ON)

# 目录常量（按 Godot 源树惯例）
set(_TP_ROOT "${CMAKE_SOURCE_DIR}/thirdparty")

# ---------------------------------------------------------------------------------------------------
# 2) 与 svg 模块的协作：仅当 MODULE_SVG_ENABLED 时，为 thorvg 提供包含目录与宏（对齐 SCsub 顶部）
# ---------------------------------------------------------------------------------------------------
if (MODULE_SVG_ENABLED)
    # 对齐：
    # env_text_server_adv.Prepend(CPPPATH=[ "#thirdparty/thorvg/inc", "#thirdparty/thorvg/src/common", "#thirdparty/thorvg/src/renderer"])
    target_include_directories(godot PRIVATE
            "${_TP_ROOT}/thorvg/inc"
            "${_TP_ROOT}/thorvg/src/common"
            "${_TP_ROOT}/thorvg/src/renderer"
    )
    # 对齐：env_text_server_adv.Append(CPPDEFINES=["TVG_STATIC"])
    target_compile_definitions(godot PRIVATE TVG_STATIC)
endif()

# ---------------------------------------------------------------------------------------------------
# 3) thirdparty: harfbuzz（可选）—— 构建为 OBJECT 库并把对象合入 godot
# ---------------------------------------------------------------------------------------------------
if (GODOT_BUILTIN_HARFBUZZ)
    # 源列表（与 SCsub 对齐）
    set(_HB_REL
            src/hb-aat-layout.cc
            src/hb-aat-map.cc
            src/hb-blob.cc
            src/hb-buffer-serialize.cc
            src/hb-buffer-verify.cc
            src/hb-buffer.cc
            src/hb-common.cc
            src/hb-draw.cc
            src/hb-face-builder.cc
            src/hb-face.cc
            src/hb-fallback-shape.cc
            src/hb-font.cc
            src/hb-icu.cc
            src/hb-map.cc
            src/hb-number.cc
            src/hb-ot-cff1-table.cc
            src/hb-ot-cff2-table.cc
            src/hb-ot-color.cc
            src/hb-ot-face.cc
            src/hb-ot-font.cc
            src/hb-ot-layout.cc
            src/hb-ot-map.cc
            src/hb-ot-math.cc
            src/hb-ot-meta.cc
            src/hb-ot-metrics.cc
            src/hb-ot-name.cc
            src/hb-ot-shaper-arabic.cc
            src/hb-ot-shaper-default.cc
            src/hb-ot-shaper-hangul.cc
            src/hb-ot-shaper-hebrew.cc
            src/hb-ot-shaper-indic-table.cc
            src/hb-ot-shaper-indic.cc
            src/hb-ot-shaper-khmer.cc
            src/hb-ot-shaper-myanmar.cc
            src/hb-ot-shaper-syllabic.cc
            src/hb-ot-shaper-thai.cc
            src/hb-ot-shaper-use.cc
            src/hb-ot-shaper-vowel-constraints.cc
            src/hb-ot-shape-fallback.cc
            src/hb-ot-shape-normalize.cc
            src/hb-ot-shape.cc
            src/hb-ot-tag.cc
            src/hb-ot-var.cc
            src/hb-outline.cc
            src/hb-paint-extents.cc
            src/hb-paint.cc
            src/hb-set.cc
            src/hb-shape-plan.cc
            src/hb-shape.cc
            src/hb-shaper.cc
            src/hb-static.cc
            src/hb-style.cc
            src/hb-subset-cff-common.cc
            src/hb-subset-cff1.cc
            src/hb-subset-cff2.cc
            src/hb-subset-input.cc
            src/hb-subset-instancer-iup.cc
            src/hb-subset-instancer-solver.cc
            src/hb-subset-plan.cc
            src/hb-subset-repacker.cc
            src/hb-subset.cc
            src/hb-ucd.cc
            src/hb-unicode.cc
            src/OT/Var/VARC/VARC.cc
    )
    if (MODULE_FREETYPE_ENABLED)
        list(APPEND _HB_REL src/hb-ft.cc)
        if (MODULE_GRAPHITE_ENABLED)
            list(APPEND _HB_REL src/hb-graphite2.cc)
        endif()
    endif()

    set(_HB_DIR "${_TP_ROOT}/harfbuzz")
    set(_HB_SRC "")
    foreach(_rel IN LISTS _HB_REL)
        list(APPEND _HB_SRC "${_HB_DIR}/${_rel}")
    endforeach()

    add_library(harfbuzz_builtin OBJECT ${_HB_SRC})

    # 对齐：包含目录
    target_include_directories(harfbuzz_builtin PRIVATE
            "${_HB_DIR}/src"
    )

    # ICU 支持（对齐：-DHAVE_ICU；以及当使用内置 ICU 时的额外宏与包含路径）
    target_compile_definitions(harfbuzz_builtin PRIVATE HAVE_ICU)
    if (GODOT_BUILTIN_ICU4C)
        target_include_directories(harfbuzz_builtin PRIVATE
                "${_TP_ROOT}/icu4c/common"
                "${_TP_ROOT}/icu4c/i18n"
        )
        target_compile_definitions(harfbuzz_builtin PRIVATE
                U_STATIC_IMPLEMENTATION
                U_HAVE_LIB_SUFFIX=1
                U_LIB_SUFFIX_C_NAME=_godot
                HAVE_ICU_BUILTIN
        )
    endif()

    # freetype / graphite 配置（仅当 freetype 模块启用）
    if (MODULE_FREETYPE_ENABLED)
        target_compile_definitions(harfbuzz_builtin PRIVATE HAVE_FREETYPE)
        if (MODULE_GRAPHITE_ENABLED)
            target_compile_definitions(harfbuzz_builtin PRIVATE HAVE_GRAPHITE2)
        endif()
        if (GODOT_BUILTIN_FREETYPE)
            target_include_directories(harfbuzz_builtin PRIVATE "${_TP_ROOT}/freetype/include")
        endif()
        if (GODOT_BUILTIN_GRAPHITE2 AND MODULE_GRAPHITE_ENABLED)
            target_include_directories(harfbuzz_builtin PRIVATE "${_TP_ROOT}/graphite/include")
            target_compile_definitions(harfbuzz_builtin PRIVATE GRAPHITE2_STATIC)
        endif()
    endif()

    # 线程宏（对齐：android / linuxbsd / web）
    if (ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR EMSCRIPTEN)
        target_compile_definitions(harfbuzz_builtin PRIVATE HAVE_PTHREAD)
    endif()

    # 不同编译器下关闭第三方警告（等价于 disable_warnings）
    if (MSVC)
        target_compile_options(harfbuzz_builtin PRIVATE /w)
    else()
        target_compile_options(harfbuzz_builtin PRIVATE -w)
    endif()

    # 将对象并入 godot，并建立依赖关系
    target_sources(godot PRIVATE $<TARGET_OBJECTS:harfbuzz_builtin>)
    add_dependencies(godot harfbuzz_builtin)

    # 同步为本模块编译提供 HB 的头（SCsub 对 text_server_adv 环境也加了包含）
    target_include_directories(godot PRIVATE "${_HB_DIR}/src")
endif()

# ---------------------------------------------------------------------------------------------------
# 4) thirdparty: graphite2（可选，且要求 freetype+graphite 模块启用）
# ---------------------------------------------------------------------------------------------------
if (GODOT_BUILTIN_GRAPHITE2 AND MODULE_FREETYPE_ENABLED AND MODULE_GRAPHITE_ENABLED)
    set(_GR2_DIR "${_TP_ROOT}/graphite")
    set(_GR2_REL
            src/gr_char_info.cpp
            src/gr_face.cpp
            src/gr_features.cpp
            src/gr_font.cpp
            src/gr_logging.cpp
            src/gr_segment.cpp
            src/gr_slot.cpp
            src/CmapCache.cpp
            src/Code.cpp
            src/Collider.cpp
            src/Decompressor.cpp
            src/Face.cpp
            src/FeatureMap.cpp
            src/Font.cpp
            src/GlyphCache.cpp
            src/GlyphFace.cpp
            src/Intervals.cpp
            src/Justifier.cpp
            src/NameTable.cpp
            src/Pass.cpp
            src/Position.cpp
            src/Segment.cpp
            src/Silf.cpp
            src/Slot.cpp
            src/Sparse.cpp
            src/TtfUtil.cpp
            src/UtfCodec.cpp
            src/FileFace.cpp
            src/json.cpp
    )
    # SCons: 非 MSVC 用 direct_machine.cpp，否则 call_machine.cpp
    if (MSVC)
        list(APPEND _GR2_REL src/call_machine.cpp)
    else()
        list(APPEND _GR2_REL src/direct_machine.cpp)
    endif()

    set(_GR2_SRC "")
    foreach(_rel IN LISTS _GR2_REL)
        list(APPEND _GR2_SRC "${_GR2_DIR}/${_rel}")
    endforeach()

    add_library(graphite_builtin OBJECT ${__GR2_SRC} ${_GR2_SRC})

    target_include_directories(graphite_builtin PRIVATE
            "${_GR2_DIR}/src"
            "${_GR2_DIR}/include"
    )
    target_compile_definitions(graphite_builtin PRIVATE
            GRAPHITE2_STATIC
            GRAPHITE2_NTRACING
            GRAPHITE2_NFILEFACE
    )

    if (MSVC)
        target_compile_options(graphite_builtin PRIVATE /w)
    else()
        target_compile_options(graphite_builtin PRIVATE -w)
    endif()

    target_sources(godot PRIVATE $<TARGET_OBJECTS:graphite_builtin>)
    add_dependencies(godot graphite_builtin)
endif()

# ---------------------------------------------------------------------------------------------------
# 5) thirdparty: ICU（可选）
# - 编辑器构建：以自定义命令将 icudt_godot.dat 转为头 icudata.gen.h，并为 godot/本模块设置宏
# - 非编辑器构建：使用 icu_data/icudata_stub.cpp
# ---------------------------------------------------------------------------------------------------
if (GODOT_BUILTIN_ICU4C)
    set(_ICU_DIR "${_TP_ROOT}/icu4c")

    set(_ICU_REL
            common/appendable.cpp
            common/bmpset.cpp
            common/brkeng.cpp
            common/brkiter.cpp
            common/bytesinkutil.cpp
            common/bytestream.cpp
            common/bytestrie.cpp
            common/bytestriebuilder.cpp
            common/bytestrieiterator.cpp
            common/caniter.cpp
            common/characterproperties.cpp
            common/chariter.cpp
            common/charstr.cpp
            common/cmemory.cpp
            common/cstr.cpp
            common/cstring.cpp
            common/cwchar.cpp
            common/dictbe.cpp
            common/dictionarydata.cpp
            common/dtintrv.cpp
            common/edits.cpp
            common/emojiprops.cpp
            common/errorcode.cpp
            common/filteredbrk.cpp
            common/filterednormalizer2.cpp
            common/icudataver.cpp
            common/icuplug.cpp
            common/loadednormalizer2impl.cpp
            common/localebuilder.cpp
            common/localematcher.cpp
            common/localeprioritylist.cpp
            common/locavailable.cpp
            common/locbased.cpp
            common/locdispnames.cpp
            common/locdistance.cpp
            common/locdspnm.cpp
            common/locid.cpp
            common/loclikely.cpp
            common/loclikelysubtags.cpp
            common/locmap.cpp
            common/locresdata.cpp
            common/locutil.cpp
            common/lsr.cpp
            common/lstmbe.cpp
            common/messagepattern.cpp
            common/mlbe.cpp
            common/normalizer2.cpp
            common/normalizer2impl.cpp
            common/normlzr.cpp
            common/parsepos.cpp
            common/patternprops.cpp
            common/pluralmap.cpp
            common/propname.cpp
            common/propsvec.cpp
            common/punycode.cpp
            common/putil.cpp
            common/rbbi.cpp
            common/rbbi_cache.cpp
            common/rbbidata.cpp
            common/rbbinode.cpp
            common/rbbirb.cpp
            common/rbbiscan.cpp
            common/rbbisetb.cpp
            common/rbbistbl.cpp
            common/rbbitblb.cpp
            common/resbund.cpp
            common/resbund_cnv.cpp
            common/resource.cpp
            common/restrace.cpp
            common/ruleiter.cpp
            common/schriter.cpp
            common/serv.cpp
            common/servlk.cpp
            common/servlkf.cpp
            common/servls.cpp
            common/servnotf.cpp
            common/servrbf.cpp
            common/servslkf.cpp
            common/sharedobject.cpp
            common/simpleformatter.cpp
            common/static_unicode_sets.cpp
            common/stringpiece.cpp
            common/stringtriebuilder.cpp
            common/uarrsort.cpp
            common/ubidi.cpp
            common/ubidi_props.cpp
            common/ubidiln.cpp
            common/ubiditransform.cpp
            common/ubidiwrt.cpp
            common/ubrk.cpp
            common/ucase.cpp
            common/ucasemap.cpp
            common/ucasemap_titlecase_brkiter.cpp
            common/ucat.cpp
            common/uchar.cpp
            common/ucharstrie.cpp
            common/ucharstriebuilder.cpp
            common/ucharstrieiterator.cpp
            common/uchriter.cpp
            common/ucln_cmn.cpp
            common/ucmndata.cpp
            common/ucnv.cpp
            common/ucnv2022.cpp
            common/ucnv_bld.cpp
            common/ucnv_cb.cpp
            common/ucnv_cnv.cpp
            common/ucnv_ct.cpp
            common/ucnv_err.cpp
            common/ucnv_ext.cpp
            common/ucnv_io.cpp
            common/ucnv_lmb.cpp
            common/ucnv_set.cpp
            common/ucnv_u16.cpp
            common/ucnv_u32.cpp
            common/ucnv_u7.cpp
            common/ucnv_u8.cpp
            common/ucnvbocu.cpp
            common/ucnvdisp.cpp
            common/ucnvhz.cpp
            common/ucnvisci.cpp
            common/ucnvlat1.cpp
            common/ucnvmbcs.cpp
            common/ucnvscsu.cpp
            common/ucnvsel.cpp
            common/ucol_swp.cpp
            common/ucptrie.cpp
            common/ucurr.cpp
            common/udata.cpp
            common/udatamem.cpp
            common/udataswp.cpp
            common/uenum.cpp
            common/uhash.cpp
            common/uhash_us.cpp
            common/uidna.cpp
            common/uinit.cpp
            common/uinvchar.cpp
            common/uiter.cpp
            common/ulist.cpp
            common/uloc.cpp
            common/uloc_keytype.cpp
            common/uloc_tag.cpp
            common/ulocale.cpp
            common/ulocbuilder.cpp
            common/umapfile.cpp
            common/umath.cpp
            common/umutablecptrie.cpp
            common/umutex.cpp
            common/unames.cpp
            common/unifiedcache.cpp
            common/unifilt.cpp
            common/unifunct.cpp
            common/uniset.cpp
            common/uniset_closure.cpp
            common/uniset_props.cpp
            common/unisetspan.cpp
            common/unistr.cpp
            common/unistr_case.cpp
            common/unistr_case_locale.cpp
            common/unistr_cnv.cpp
            common/unistr_props.cpp
            common/unistr_titlecase_brkiter.cpp
            common/unorm.cpp
            common/unormcmp.cpp
            common/uobject.cpp
            common/uprops.cpp
            common/ures_cnv.cpp
            common/uresbund.cpp
            common/uresdata.cpp
            common/usc_impl.cpp
            common/uscript.cpp
            common/uscript_props.cpp
            common/uset.cpp
            common/uset_props.cpp
            common/usetiter.cpp
            common/usprep.cpp
            common/ustack.cpp
            common/ustr_cnv.cpp
            common/ustr_titlecase_brkiter.cpp
            common/ustr_wcs.cpp
            common/ustrcase.cpp
            common/ustrcase_locale.cpp
            common/ustrenum.cpp
            common/ustrfmt.cpp
            common/ustring.cpp
            common/ustrtrns.cpp
            common/utext.cpp
            common/utf_impl.cpp
            common/util.cpp
            common/util_props.cpp
            common/utrace.cpp
            common/utrie.cpp
            common/utrie2.cpp
            common/utrie2_builder.cpp
            common/utrie_swap.cpp
            common/uts46.cpp
            common/utypes.cpp
            common/uvector.cpp
            common/uvectr32.cpp
            common/uvectr64.cpp
            common/wintz.cpp
            i18n/scriptset.cpp
            i18n/ucln_in.cpp
            i18n/uspoof.cpp
            i18n/uspoof_impl.cpp
    )

    set(_ICU_SRC "")
    foreach(_rel IN LISTS _ICU_REL)
        list(APPEND _ICU_SRC "${_ICU_DIR}/${_rel}")
    endforeach()

    # 编辑器构建：生成 icudata.gen.h（等价于 SCons 的 Command + make_icu_data）
    # 注：使用 Python -c 读取 icudt_godot.dat 并生成对应的 C 头
    if (GODOT_EDITOR_BUILD)
        set(_ICU_DAT "${_ICU_DIR}/icudt_godot.dat")
        set(_ICU_GEN_H "${_ICU_DIR}/icudata.gen.h")

        add_custom_command(
                OUTPUT "${_ICU_GEN_H}"
                COMMAND ${CMAKE_COMMAND} -E echo "Generating ICU data header: ${_ICU_GEN_H}"
                COMMAND ${Python_EXECUTABLE} -c
                "import sys, pathlib; p=pathlib.Path(r'${_ICU_DAT}'); b=p.read_bytes();\
h=pathlib.Path(r'${_ICU_GEN_H}');\
with h.open('w', encoding='utf-8', newline='\\n') as g:\
 g.write('/* THIS FILE IS GENERATED DO NOT EDIT */\\n');\
 g.write('/* (C) 2016 and later: Unicode, Inc. and others. */\\n');\
 g.write('/* License & terms of use: https://www.unicode.org/copyright.html */\\n');\
 g.write('#ifndef _ICU_DATA_H\\n#define _ICU_DATA_H\\n');\
 g.write('#include \"unicode/utypes.h\"\\n');\
 g.write('#include \"unicode/udata.h\"\\n');\
 g.write('#include \"unicode/uversion.h\"\\n');\
 g.write('extern \"C\" U_EXPORT const size_t U_ICUDATA_SIZE = ' + str(len(b)) + ';\\n');\
 g.write('extern \"C\" U_EXPORT const unsigned char U_ICUDATA_ENTRY_POINT[] = {\\n');\
 g.writelines('\\t' + str(x) + ',\\n' for x in b);\
 g.write('};\\n#endif');"
                DEPENDS "${_ICU_DAT}"
                VERBATIM
        )
        # 确保 Python 可用（CMake 3.12+）
        if (NOT Python_EXECUTABLE)
            find_package(Python3 COMPONENTS Interpreter REQUIRED)
            set(Python_EXECUTABLE "${Python3_EXECUTABLE}")
        endif()
    endif()

    # 若为非编辑器构建，追加 ICU stub 源
    set(_ICU_STUB "")
    if (NOT GODOT_EDITOR_BUILD)
        list(APPEND _ICU_STUB "${CMAKE_CURRENT_SOURCE_DIR}/icu_data/icudata_stub.cpp")
    endif()

    add_library(icu_builtin OBJECT ${_ICU_SRC} ${_ICU_STUB})

    # 包含目录（公共 + i18n）
    target_include_directories(icu_builtin PRIVATE
            "${_ICU_DIR}/common"
            "${_ICU_DIR}/i18n"
    )
    # 若生成了 icudata.gen.h，确保包含根（供本模块/其他源码引用）
    if (GODOT_EDITOR_BUILD)
        add_custom_target(icu_data_header DEPENDS "${_ICU_DIR}/icudata.gen.h")
        add_dependencies(icu_builtin icu_data_header)
        target_include_directories(icu_builtin PRIVATE "${_ICU_DIR}")
        # 本模块需要看到 ICU_STATIC_DATA 宏
        target_compile_definitions(godot PRIVATE ICU_STATIC_DATA)
        target_compile_definitions(icu_builtin PRIVATE ICU_STATIC_DATA)
    endif()

    # ICU 宏（对齐 SCsub）
    target_compile_definitions(icu_builtin PRIVATE
            U_STATIC_IMPLEMENTATION
            U_COMMON_IMPLEMENTATION
            UCONFIG_NO_COLLATION
            UCONFIG_NO_CONVERSION
            UCONFIG_NO_FORMATTING
            UCONFIG_NO_SERVICE
            UCONFIG_NO_IDNA
            UCONFIG_NO_FILE_IO
            UCONFIG_NO_TRANSLITERATION
            UCONFIG_NO_REGULAR_EXPRESSIONS
            PKGDATA_MODE=static
            U_ENABLE_DYLOAD=0
            U_HAVE_LIB_SUFFIX=1
            U_LIB_SUFFIX_C_NAME=_godot
    )
    # text_server_adv 自己也带的 ICU 宏（对齐 env_text_server_adv.Append）
    target_compile_definitions(godot PRIVATE
            U_STATIC_IMPLEMENTATION
            U_HAVE_LIB_SUFFIX=1
            U_LIB_SUFFIX_C_NAME=_godot
    )

    # 关闭 warnings
    if (MSVC)
        target_compile_options(icu_builtin PRIVATE /w)
    else()
        target_compile_options(icu_builtin PRIVATE -w)
    endif()

    # 合入 godot，并建立依赖
    target_sources(godot PRIVATE $<TARGET_OBJECTS:icu_builtin>)
    add_dependencies(godot icu_builtin)
endif()

# ---------------------------------------------------------------------------------------------------
# 6) 本模块本体源码与公共编译条件
# ---------------------------------------------------------------------------------------------------
# msdfgen 头的处理：非 MSVC 下作为 system 头以抑制警告；并定义 MSDFGEN_PUBLIC 为空
if (GODOT_BUILTIN_MSDFGEN AND MODULE_MSDFGEN_ENABLED)
    # CMake 中用选项开关表示（若上层未提供，可在此定义）
    option(GODOT_BUILTIN_MSDFGEN "Use bundled msdfgen headers" ON)
    if (NOT MSVC)
        target_include_directories(godot SYSTEM PRIVATE "${_TP_ROOT}/msdfgen")
    else()
        target_include_directories(godot PRIVATE "${_TP_ROOT}/msdfgen")
    endif()
    target_compile_definitions(godot PRIVATE MSDFGEN_PUBLIC=)
endif()

# freetype 头与 graphite 头
if (GODOT_BUILTIN_FREETYPE AND MODULE_FREETYPE_ENABLED)
    target_compile_definitions(godot PRIVATE FT_CONFIG_OPTION_USE_BROTLI)
    target_include_directories(godot PRIVATE "${_TP_ROOT}/freetype/include")
endif()
if (GODOT_BUILTIN_GRAPHITE2 AND MODULE_FREETYPE_ENABLED AND MODULE_GRAPHITE_ENABLED)
    target_include_directories(godot PRIVATE "${_TP_ROOT}/graphite/include")
endif()

# text_server_adv 自身需要能看到 ICU 的公共头（当内置时）
if (GODOT_BUILTIN_ICU4C)
    target_include_directories(godot PRIVATE
            "${_TP_ROOT}/icu4c/common"
            "${_TP_ROOT}/icu4c/i18n"
    )
endif()

# 收集模块本地 *.cpp
file(GLOB CONFIGURE_DEPENDS TEXT_SERVER_ADV_LOCAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
# 注入到 godot 目标
target_sources(godot PRIVATE ${TEXT_SERVER_ADV_LOCAL_SOURCES})

# 友好提示
message(STATUS "[text_server_adv] SVG:${MODULE_SVG_ENABLED} Freetype:${MODULE_FREETYPE_ENABLED} MSDFGEN:${MODULE_MSDFGEN_ENABLED} Graphite:${MODULE_GRAPHITE_ENABLED}")
message(STATUS "[text_server_adv] builtin: HB=${GODOT_BUILTIN_HARFBUZZ} GR2=${GODOT_BUILTIN_GRAPHITE2} FT=${GODOT_BUILTIN_FREETYPE} ICU=${GODOT_BUILTIN_ICU4C} editor=${GODOT_EDITOR_BUILD}")