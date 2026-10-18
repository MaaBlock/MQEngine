include("../../Config/copy_headers.cmake")
include("../../Config/set_utf8_compiler_encoding.cmake")

file(GLOB IMGUI_PLUGIN_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/*.cpp"
)

file(GLOB IMGUI_PLUGIN_HEADERS
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/*.h"
        "./src/thirdparty/*.h"
)

if(ANDROID)
    set(IMGUI_PLUGIN_LIB_TYPE STATIC)
else()
    set(IMGUI_PLUGIN_LIB_TYPE SHARED)
endif()

add_library(ImguiPlugin ${IMGUI_PLUGIN_LIB_TYPE}
        ${IMGUI_PLUGIN_SOURCES}
        ${IMGUI_PLUGIN_HEADERS}
)

target_compile_definitions(ImguiPlugin PRIVATE BUILD_IMGUI_PLUGIN)

add_copy_target_headers(ImguiPlugin "${IMGUI_PLUGIN_HEADERS}")

target_link_libraries(ImguiPlugin
        FCT_IMGUI
)
target_link_fct_libraries(ImguiPlugin)
target_link_fct_node_libraries(ImguiPlugin)
target_link_engine(ImguiPlugin)
set_utf8_for_target(ImguiPlugin)

target_include_directories(ImguiPlugin PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

function(target_link_imgui_plugin target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Target ${target_name} does not exist")
    endif()

    target_link_libraries(${target_name} ImguiPlugin)
    add_dependencies(${target_name} ImguiPlugin)

    if(WIN32)
        add_custom_target(${target_name}_copy_imgui_plugin_dll ALL
                COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target_name}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:ImguiPlugin>
                $<TARGET_FILE_DIR:${target_name}>
                DEPENDS ImguiPlugin
                COMMENT "Copying ImguiPlugin.dll to ${target_name} directory"
        )
        add_dependencies(${target_name} ${target_name}_copy_imgui_plugin_dll)
    endif()

    message(STATUS "Configured ImguiPlugin for target: ${target_name}")
endfunction()
