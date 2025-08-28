# 子目录 CMakeLists：camera 模块

# 收集平台相关源
set(CAMERA_SOURCES)

# 桌面平台都需要 register_types.cpp
if(WIN32 OR APPLE OR (UNIX AND NOT APPLE))
    list(APPEND CAMERA_SOURCES
            register_types.cpp
    )
endif()

# 分平台添加实现文件
if(WIN32)
    list(APPEND CAMERA_SOURCES
            camera_win.cpp
    )
elseif(APPLE)
    list(APPEND CAMERA_SOURCES
            camera_macos.mm
    )
    # 确保以 Objective-C++ 方式编译 .mm 源
    set_source_files_properties(camera_macos.mm PROPERTIES LANGUAGE OBJCXX)
elseif(UNIX AND NOT APPLE)
    list(APPEND CAMERA_SOURCES
            camera_linux.cpp
            camera_feed_linux.cpp
            buffer_decoder.cpp
    )
endif()

# 将本模块的源并入已存在的目标 godot
if(CAMERA_SOURCES)
    target_sources(godot PRIVATE ${CAMERA_SOURCES})
endif()