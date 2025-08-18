
file(GLOB CORE_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/core/*.cpp"
)

file(GLOB CODE_GENERATOR_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/codeGenerator/*.cpp"
)

file(GLOB UI_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/imgui/*.cpp"
)


add_executable(Editor
        ${CORE_MODULE_SOURCES}
        ${CODE_GENERATOR_MODULE_SOURCES}
        ${UI_MODULE_SOURCES}
)
target_link_libraries(Editor
    FCT_IMGUI
)
target_link_fct_libraries(Editor)
target_link_fct_node_libraries(Editor)
target_link_engine(Editor)