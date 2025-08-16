
file(GLOB CORE_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/core/*.cpp"
)

add_executable(Editor
    ${CORE_MODULE_SOURCES}
)
target_link_libraries(Editor
    FCT_IMGUI
)
target_link_fct_libraries(Editor)
target_link_fct_node_libraries(Editor)
target_link_engine(Editor)