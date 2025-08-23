
file(GLOB CORE_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/core/*.cpp"
)

file(GLOB RENDERGRAPHVIEW_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/RenderGraphView/*.cpp"
)


file(GLOB RENDERGRAPHVIEW_MODULE_HEADERS
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/RenderGraphView/*.h"
        "./src/RenderGraphView/*.hpp"
)

file(GLOB UI_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/imgui/*.cpp"
)
set(MQENGINE_EDITOR_SOURCES
        ${CORE_MODULE_SOURCES}
        ${RENDERGRAPHVIEW_MODULE_SOURCES}
        ${UI_MODULE_SOURCES}
)
set(MQENGINE_EDITOR_HEADERS
        ${RENDERGRAPHVIEW_MODULE_HEADERS}
)

add_executable(Editor
        ${MQENGINE_EDITOR_SOURCES}
        ${MQENGINE_EDITOR_HEADERS}
)
target_link_libraries(Editor
    FCT_IMGUI
)
target_link_fct_libraries(Editor)
target_link_fct_node_libraries(Editor)
target_link_engine(Editor)