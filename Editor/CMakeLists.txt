
add_executable(Editor
    src/test.cpp
)
target_link_libraries(Editor
    FCT_IMGUI
)
target_link_engine(Editor)
target_link_fct_node_libraries(Editor)