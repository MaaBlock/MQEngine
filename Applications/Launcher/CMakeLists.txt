include("../../Config/set_utf8_compiler_encoding.cmake")
file(GLOB CORE_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/core/*.cpp"
)
file(GLOB CORE_MODULE_HEADERS
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/core/*.h"
        "./src/core/*.hpp"
)
file(GLOB THIRDPARTY_MODULE_HEADERS
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/thirdparty/*.h"
)

set(MQENGINE_LAUNCHER_SOURCES
        ${CORE_MODULE_SOURCES}
        "${CMAKE_SOURCE_DIR}/ThirdParty/tinyfiledialogs/tinyfiledialogs.c"
)
set(MQENGINE_LAUNCHER_HEADERS
        ${CORE_MODULE_HEADERS}
        ${THIRDPARTY_MODULE_HEADERS}
)

add_executable(Launcher
        ${MQENGINE_LAUNCHER_SOURCES}
        ${MQENGINE_LAUNCHER_HEADERS}
)

target_include_directories(Launcher PUBLIC
        "${CMAKE_SOURCE_DIR}/ThirdParty/tinyfiledialogs"
)

target_link_libraries(Launcher
        FCT_IMGUI
)
target_link_fct_libraries(Launcher)
target_link_fct_node_libraries(Launcher)
target_link_engine(Launcher)
set_utf8_for_target(Launcher)

if(TARGET Game)
    add_dependencies(Launcher Game)
endif()
if(TARGET Editor)
    add_dependencies(Launcher Editor)
endif()