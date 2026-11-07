include("../../Config/set_utf8_compiler_encoding.cmake")
include("../../Config/copy_headers.cmake")

add_subdirectory(LauncherProject)

collect_module_files(LAUNCHER "./src")

set(MQENGINE_LAUNCHER_SOURCES
        ${LAUNCHER_MODULE_SOURCES}
)
set(MQENGINE_LAUNCHER_HEADERS
        ${LAUNCHER_MODULE_HEADERS}
)

add_executable(Launcher
        ${MQENGINE_LAUNCHER_SOURCES}
        ${MQENGINE_LAUNCHER_HEADERS}
)

target_link_fct_libraries(Launcher)
target_link_fct_node_libraries(Launcher)
target_link_engine(Launcher)
target_link_launcher_native(Launcher)
set_utf8_for_target(Launcher)

if(TARGET Game)
    add_dependencies(Launcher Game)
endif()
if(TARGET Editor)
    add_dependencies(Launcher Editor)
endif()
