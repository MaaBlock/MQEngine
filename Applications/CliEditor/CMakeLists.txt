include("../../Config/set_utf8_compiler_encoding.cmake")
include("../../Config/copy_headers.cmake")

collect_module_files(CLIEDITOR "./src")

add_executable(CliEditor
    ${CLIEDITOR_MODULE_SOURCES}
    ${CLIEDITOR_MODULE_HEADERS}
)

target_link_fct_libraries(CliEditor)
target_link_fct_node_libraries(CliEditor)
target_link_engine(CliEditor)
set_utf8_for_target(CliEditor)

if(TARGET MQCliEditor)
    add_dependencies(CliEditor MQCliEditor)
endif()
