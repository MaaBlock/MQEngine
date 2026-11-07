cmake_minimum_required(VERSION 3.22.1)
project(ImguiPlugin LANGUAGES CXX)

if(NOT DEFINED MQENGINE_ROOT_DIR)
    get_filename_component(MQENGINE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    if(NOT EXISTS "${MQENGINE_ROOT_DIR}/Config/MQPluginSDK.cmake")
        get_filename_component(MQENGINE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
    endif()
endif()
include("${MQENGINE_ROOT_DIR}/Config/MQPluginSDK.cmake")

mq_add_plugin(ImguiPlugin
    BUILTIN
    VERSION "1.0.0"
)

function(target_link_imgui_plugin target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Target ${target_name} does not exist")
    endif()

    target_link_libraries(${target_name} ImguiPlugin)
    if(TARGET ImguiPlugin AND NOT MQ_PLUGIN_STANDALONE_BUILD)
        add_dependencies(${target_name} ImguiPlugin)
    endif()

    if(WIN32 AND NOT MQ_PLUGIN_STANDALONE_BUILD)
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
