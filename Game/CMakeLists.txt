include("../Config/set_utf8_compiler_encoding.cmake")
file(GLOB CORE_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/core/*.cpp"
)

set(MQENGINE_GAME_SOURCES
        ${CORE_MODULE_SOURCES}
)

add_executable(Game
        ${MQENGINE_GAME_SOURCES}
)

target_link_libraries(Game
        FCT_IMGUI
)
target_link_fct_libraries(Game)
target_link_fct_node_libraries(Game)
target_link_engine(Game)
set_utf8_for_target(Game)