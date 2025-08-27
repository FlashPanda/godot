# CMake

# 收集本目录下所有 .cpp 源文件
file(GLOB LOCAL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 将这些源文件添加到已存在的目标 godot
target_sources(godot PRIVATE ${LOCAL_SOURCES})

# 检测 mntent.h 并在存在时定义 HAVE_MNTENT
include(CheckIncludeFile)
check_include_file("mntent.h" HAVE_MNTENT)

if(HAVE_MNTENT)
    target_compile_definitions(godot PRIVATE HAVE_MNTENT)
endif()