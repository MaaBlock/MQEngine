include("../../Config/set_utf8_compiler_encoding.cmake")
include("../../Config/copy_headers.cmake")

collect_module_files(GAME "./src")

add_executable(Game
        ${GAME_MODULE_SOURCES}
        ${GAME_MODULE_HEADERS}
)

target_link_libraries(Game
        FCT_IMGUI
)
target_link_fct_libraries(Game)
target_link_fct_node_libraries(Game)
target_link_engine(Game)
target_link_imgui_plugin(Game)
target_link_3dtiles_plugin(Game)
target_link_homelbm_plugin(Game)
target_link_user_code(Game)
set_utf8_for_target(Game)
