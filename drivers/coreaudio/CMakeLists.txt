file(GLOB LOCAL_SOURCES "*.mm")

# 直接把这些源附加到可执行目标 godot
target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
)
