# 子目录 CMakeLists.txt
# 等价于 SCons 中：
#   - thirdparty_dir = "#thirdparty/spirv-reflect/"
#   - thirdparty_sources = ["spirv_reflect.c"]
#   - 禁用警告
#   - 将这些源追加到 env.servers_sources（相当于加入父目标的源列表）

# 依项目根路径定位 thirdparty 目录（SCons 中的前缀 # 表示仓库根）
set(THIRDPARTY_SPVREFLECT_DIR "${PROJECT_SOURCE_DIR}/thirdparty/spirv-reflect")

# 需要参与编译的源文件列表
set(SPIRV_REFLECT_SOURCES
        "${THIRDPARTY_SPVREFLECT_DIR}/spirv_reflect.c"
)

# 仅对上述第三方源禁用告警（不影响父目标的其它源）
if(MSVC)
    # MSVC: /w 关闭所有级别告警（仅用于该源文件）
    set_source_files_properties(${SPIRV_REFLECT_SOURCES}
            PROPERTIES
            COMPILE_OPTIONS "/w"
    )
else()
    # GCC/Clang: -w 关闭所有告警（仅用于该源文件）
    set_source_files_properties(${SPIRV_REFLECT_SOURCES}
            PROPERTIES
            COMPILE_OPTIONS "-w"
    )
endif()

# 包含目录（spirv_reflect.c 会包含同目录下的头文件）
set(SPIRV_REFLECT_INCLUDE_DIR "${THIRDPARTY_SPVREFLECT_DIR}")

# 首选路径：若上层通过变量 GODOT_SERVERS_TARGET 指定了父目标且该目标存在，
# 则把源文件直接追加到父目标，并为父目标增加私有包含目录。
if(DEFINED GODOT_SERVERS_TARGET AND TARGET "${GODOT_SERVERS_TARGET}")
    # 将第三方源并入父目标（等价于 SCons 的 add_source_files 到 env.servers_sources）
    target_sources("${GODOT_SERVERS_TARGET}" PRIVATE ${SPIRV_REFLECT_SOURCES})

    # 为父目标添加包含目录，供该源文件使用（不向外传播）
    target_include_directories("${GODOT_SERVERS_TARGET}" PRIVATE "${SPIRV_REFLECT_INCLUDE_DIR}")

    # 备选路径：未指定父目标，则创建一个 OBJECT 库供父级聚合。
    # 父级可使用：target_link_libraries(<你的父目标> PRIVATE spirv_reflect_thirdparty)
else()
    add_library(spirv_reflect_thirdparty OBJECT ${SPIRV_REFLECT_SOURCES})

    target_include_directories(spirv_reflect_thirdparty PRIVATE "${SPIRV_REFLECT_INCLUDE_DIR}")

    # 作为兜底策略，也为该对象库整体禁用告警（与每源禁警叠加不冲突）
    if(MSVC)
        target_compile_options(spirv_reflect_thirdparty PRIVATE /w)
    else()
        target_compile_options(spirv_reflect_thirdparty PRIVATE -w)
    endif()
endif()