include("../../Config/set_utf8_compiler_encoding.cmake")
include("../../Config/copy_headers.cmake")

collect_module_files(EDITORCLI "./src")

add_executable(EditorCli
        ${EDITORCLI_MODULE_SOURCES}
        ${EDITORCLI_MODULE_HEADERS}
)

target_include_directories(EditorCli PUBLIC
        "${CMAKE_SOURCE_DIR}/Engine/Core/src"
)

target_link_libraries(EditorCli
        FCT_IMGUI
)
target_link_fct_libraries(EditorCli)
target_link_fct_node_libraries(EditorCli)
target_link_engine(EditorCli)
target_link_imgui_plugin(EditorCli)
target_link_3dtiles_plugin(EditorCli)
target_link_homelbm_plugin(EditorCli)
target_link_user_code(EditorCli)
set_utf8_for_target(EditorCli)
