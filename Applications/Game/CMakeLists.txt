include("../../Config/set_utf8_compiler_encoding.cmake")
include("../../Config/copy_headers.cmake")

collect_module_files(GAME "./src")

add_executable(Game
    ${GAME_MODULE_SOURCES}
    ${GAME_MODULE_HEADERS}
)

target_link_fct_libraries(Game)
target_link_fct_node_libraries(Game)
target_link_engine(Game)
set_utf8_for_target(Game)

if(TARGET GameHtmlUI)
    add_dependencies(Game GameHtmlUI)
endif()
