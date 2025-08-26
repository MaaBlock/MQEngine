
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

file(GLOB ASSETSMANAGER_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/AssetsManager/*.cpp"
)
file(GLOB SCENEVIEWER_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/SceneViewer/*.cpp"
)
file(GLOB SCENEVIEWER_MODULE_HEADERS
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/SceneViewer/*.h"
        "./src/SceneViewer/*.hpp"
)
file(GLOB DATA_MODULE_HEADERS
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/Data/*.h"
        "./src/Data/*.hpp"
)
file(GLOB DATA_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/Data/*.cpp"
)
file(GLOB TYPE_MODULE_HEADERS
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/type/*.h"
        "./src/type/*.hpp"
)
file(GLOB TYPE_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/type/*.cpp"
)


set(MQENGINE_EDITOR_SOURCES
        ${CORE_MODULE_SOURCES}
        ${RENDERGRAPHVIEW_MODULE_SOURCES}
        ${UI_MODULE_SOURCES}
        ${ASSETSMANAGER_MODULE_SOURCES}
        ${SCENEVIEWER_MODULE_SOURCES}
        ${DATA_MODULE_SOURCES}
)
set(MQENGINE_EDITOR_HEADERS
        ${RENDERGRAPHVIEW_MODULE_HEADERS}
        ${SCENEVIEWER_MODULE_HEADERS}
        ${DATA_MODULE_HEADERS}
)
if(MSVC)
    add_compile_options(/utf-8)
endif()



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