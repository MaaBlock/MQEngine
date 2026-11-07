include("../../Config/set_utf8_compiler_encoding.cmake")
include("../../Config/copy_headers.cmake")

collect_module_files(CORE            "./src/core")
collect_module_files(RENDERGRAPHVIEW "./src/RenderGraphView")
collect_module_files(UI              "./src/imgui")
collect_module_files(ASSETSMANAGER   "./src/AssetsManager")
collect_module_files(SCENEVIEWER     "./src/SceneViewer")
collect_module_files(DATA            "./src/Data")
collect_module_files(TYPE            "./src/type")
collect_module_files(SHADEREDITOR    "./src/ShaderEditor")
collect_module_files(EDITOR_UI_LAYER "./src/UI")
collect_module_files(EDITOR_UIBEHAVIOR "./src/UIBehavior")
collect_module_files(COMMAND         "./src/command")
collect_module_files(VIEWPORT        "./src/viewport")

set(MQENGINE_EDITOR_SOURCES
        ${CORE_MODULE_SOURCES}
        ${RENDERGRAPHVIEW_MODULE_SOURCES}
        ${UI_MODULE_SOURCES}
        ${EDITOR_UI_LAYER_MODULE_SOURCES}
        ${EDITOR_UIBEHAVIOR_MODULE_SOURCES}
        ${ASSETSMANAGER_MODULE_SOURCES}
        ${SCENEVIEWER_MODULE_SOURCES}
        ${DATA_MODULE_SOURCES}
        ${SHADEREDITOR_MODULE_SOURCES}
        ${COMMAND_MODULE_SOURCES}
        ${VIEWPORT_MODULE_SOURCES}
        "${CMAKE_SOURCE_DIR}/ThirdParty/tinyfiledialogs/tinyfiledialogs.c"
)
set(MQENGINE_EDITOR_HEADERS
        ${RENDERGRAPHVIEW_MODULE_HEADERS}
        ${SCENEVIEWER_MODULE_HEADERS}
        ${DATA_MODULE_HEADERS}
        ${SHADEREDITOR_MODULE_HEADERS}
        ${EDITOR_UI_LAYER_MODULE_HEADERS}
        ${EDITOR_UIBEHAVIOR_MODULE_HEADERS}
        ${COMMAND_MODULE_HEADERS}
        ${VIEWPORT_MODULE_HEADERS}
)


add_executable(Editor
        ${MQENGINE_EDITOR_SOURCES}
        ${MQENGINE_EDITOR_HEADERS}
)
target_include_directories(Editor PUBLIC
        "${CMAKE_SOURCE_DIR}/ThirdParty/tinyfiledialogs"
        "${CMAKE_SOURCE_DIR}/Engine/Core/src"
        )
target_link_libraries(Editor
    FCT_IMGUI
    ImGuiColorTextEdit
)
target_link_fct_libraries(Editor)
target_link_fct_node_libraries(Editor)
target_link_engine(Editor)
target_link_imgui_plugin(Editor)
target_link_editor_ui_plugin(Editor)
target_link_3dtiles_plugin(Editor)
target_link_homelbm_plugin(Editor)
target_link_user_code(Editor)
set_utf8_for_target(Editor)
fct_embed_resource(
        TARGET Editor
        FILES "${CMAKE_SOURCE_DIR}/Assets/Icons/Mesh.png"
        FILES "${CMAKE_SOURCE_DIR}/Assets/Icons/Texture.png"
        FILES "${CMAKE_SOURCE_DIR}/Assets/Icons/Material.png"
        PREFIX "g_editorIcon"
)