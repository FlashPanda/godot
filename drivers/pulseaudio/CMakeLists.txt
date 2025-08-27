# 子目录 CMakeLists.txt

# 可由父级通过 -DPULSEAUDIO=ON/OFF -DUSE_SOWRAP=ON/OFF 控制
option(PULSEAUDIO "Enable PulseAudio driver in this dir" OFF)
option(USE_SOWRAP "Use pulse-so_wrap.c (dlopen style wrap)" OFF)

# 父作用域中用于收集 driver 源的聚合变量
if(NOT DEFINED GODOT_DRIVERS_SOURCES)
    set(GODOT_DRIVERS_SOURCES "" CACHE INTERNAL "" FORCE)
endif()

# 收集本目录所有 C++ 源文件
file(GLOB DRIVER_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 按条件加入 pulse-so_wrap.c
set(OPTIONAL_SOURCES "")
if(PULSEAUDIO AND USE_SOWRAP)
    list(APPEND OPTIONAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/pulse-so_wrap.c")
endif()

# 将本目录的源文件（含可选 C 源）追加到父作用域聚合变量
set(GODOT_DRIVERS_SOURCES
        ${GODOT_DRIVERS_SOURCES}
        ${DRIVER_LOCAL_SOURCES}
        ${OPTIONAL_SOURCES}
        PARENT_SCOPE
)