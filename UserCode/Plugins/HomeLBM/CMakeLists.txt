cmake_minimum_required(VERSION 3.22.1)
project(HomeLBM LANGUAGES CXX)

if(NOT DEFINED MQENGINE_ROOT_DIR)
    get_filename_component(MQENGINE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    if(NOT EXISTS "${MQENGINE_ROOT_DIR}/Config/MQPluginSDK.cmake")
        get_filename_component(MQENGINE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
    endif()
endif()
include("${MQENGINE_ROOT_DIR}/Config/MQPluginSDK.cmake")

mq_add_plugin(HomeLBM
    VERSION "1.0.0"
    DEPENDS
        ImguiPlugin
    USE_COMPUTE
    AUTO_REFLECT
    REFLECT_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/HomeLbmComponents.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/HomeLbmService.h"
    INCLUDE_DIRS
        "${MQENGINE_ROOT_DIR}/FCT/FCT_Compute/ThirdParty/LuisaCompute/src/backends/dx"
)

if(WIN32)
    target_link_libraries(HomeLBM d3d12 dxgi)
endif()

function(target_link_homelbm_plugin target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Target ${target_name} does not exist")
    endif()

    target_link_libraries(${target_name} HomeLBM)
    if(TARGET HomeLBM AND NOT MQ_PLUGIN_STANDALONE_BUILD)
        add_dependencies(${target_name} HomeLBM)
    endif()

    if(COMMAND fct_compute_deploy_runtime)
        fct_compute_deploy_runtime(${target_name})
    endif()

    if(WIN32 AND NOT MQ_PLUGIN_STANDALONE_BUILD)
        add_custom_target(${target_name}_copy_homelbm_plugin_dll ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target_name}>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:HomeLBM>
            $<TARGET_FILE_DIR:${target_name}>
            DEPENDS HomeLBM
            COMMENT "Copying HomeLBM.dll to ${target_name} directory"
        )
        add_dependencies(${target_name} ${target_name}_copy_homelbm_plugin_dll)
    endif()

    message(STATUS "Configured HomeLBM plugin for target: ${target_name}")
endfunction()
