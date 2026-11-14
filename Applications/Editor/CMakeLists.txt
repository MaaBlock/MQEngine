include("../../Config/set_utf8_compiler_encoding.cmake")
include("../../Config/copy_headers.cmake")

collect_module_files(EDITOR "./src")

add_executable(Editor
    ${EDITOR_MODULE_SOURCES}
    ${EDITOR_MODULE_HEADERS}
)

target_link_fct_libraries(Editor)
target_link_fct_node_libraries(Editor)
target_link_engine(Editor)
set_utf8_for_target(Editor)

if(TARGET EditorBase)
    add_dependencies(Editor EditorBase)
endif()
if(TARGET MQEditor)
    add_dependencies(Editor MQEditor)
endif()
if(TARGET GameHtmlUI)
    add_dependencies(Editor GameHtmlUI)
endif()
