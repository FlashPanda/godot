# 子目录 CMakeLists.txt（放在含有 *.cpp 的目录内）

# 收集本目录所有 cpp 源文件
file(GLOB XAUDIO2_SOURCES CONFIGURE_DEPENDS
        "*.cpp"
)

# 构建为静态库（也可以改为 OBJECT，看你的集成方式）
add_library(xaudio2_driver STATIC
        ${XAUDIO2_SOURCES}
)

# C++17
target_compile_features(xaudio2_driver PRIVATE cxx_std_17)

# 等价于 CPPDEFINES=["XAUDIO2_ENABLED"]
target_compile_definitions(xaudio2_driver PRIVATE XAUDIO2_ENABLED)

# 等价于 LINKFLAGS=["xaudio2_8.lib"]，仅在 Windows 下生效
if(WIN32)
    target_link_libraries(xaudio2_driver PRIVATE xaudio2_8)
endif()