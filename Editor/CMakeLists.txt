
add_executable(Editor
    src/test.cpp
)
target_link_libraries(Editor PRIVATE
    FCT_IMGUI
)
target_link_engine(Editor)