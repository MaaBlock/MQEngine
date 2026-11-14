include_guard(GLOBAL)

include("${CMAKE_SOURCE_DIR}/Config/set_utf8_compiler_encoding.cmake")
include("${CMAKE_SOURCE_DIR}/Config/copy_headers.cmake")

# ==============================================================================
# mq_add_composite(<CompositeName>
#     PLUGIN <PluginName>
#     [DEPENDS <DepComposite1> <DepComposite2> ...]
#     [SOURCES <Src1> <Src2> ...]
# )
# 每个 Composite 编译为独立的动态链接库 (<CompositeName>.dll)
# ==============================================================================
function(mq_add_composite COMP_FIRST_ARG)
    set(options "")
    set(oneValueArgs NAME PLUGIN PLUGIN_NAME)
    set(multiValueArgs SOURCES PUBLIC_HEADERS INCLUDES LIBS DEPENDS TAGS)
    cmake_parse_arguments(MQ_COMP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(MQ_COMP_NAME)
        set(COMP_NAME "${MQ_COMP_NAME}")
    else()
        set(COMP_NAME "${COMP_FIRST_ARG}")
    endif()

    if(MQ_COMP_PLUGIN)
        set(PLUGIN_NAME "${MQ_COMP_PLUGIN}")
    elseif(MQ_COMP_PLUGIN_NAME)
        set(PLUGIN_NAME "${MQ_COMP_PLUGIN_NAME}")
    else()
        message(FATAL_ERROR "mq_add_composite(${COMP_NAME}): 必须指定 PLUGIN <PluginName>")
    endif()

    if(MQ_COMP_SOURCES)
        set(COMP_SOURCES ${MQ_COMP_SOURCES})
        set(COMP_HEADERS ${MQ_COMP_PUBLIC_HEADERS})
    else()
        collect_module_files(${COMP_NAME}_MOD "${CMAKE_CURRENT_SOURCE_DIR}/src/Composites/${COMP_NAME}")
        set(COMP_SOURCES ${${COMP_NAME}_MOD_MODULE_SOURCES})
        set(COMP_HEADERS ${${COMP_NAME}_MOD_MODULE_HEADERS})
    endif()

    add_library(${COMP_NAME} SHARED
        ${COMP_SOURCES}
        ${COMP_HEADERS}
    )

    string(TOUPPER "${COMP_NAME}" UPPER_COMP_NAME)
    target_compile_definitions(${COMP_NAME} PRIVATE
        "BUILD_${UPPER_COMP_NAME}_COMPOSITE"
        "BUILD_PLUGIN_${PLUGIN_NAME}"
    )

    target_include_directories(${COMP_NAME} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )

    if(MQ_COMP_INCLUDES)
        target_include_directories(${COMP_NAME} PUBLIC ${MQ_COMP_INCLUDES})
    endif()

    target_link_fct_libraries(${COMP_NAME})
    target_link_fct_node_libraries(${COMP_NAME})
    target_link_fct_imgui_libraries(${COMP_NAME})
    target_link_engine(${COMP_NAME})
    set_utf8_for_target(${COMP_NAME})

    if(MQ_COMP_DEPENDS)
        target_link_libraries(${COMP_NAME} ${MQ_COMP_DEPENDS})
        add_dependencies(${COMP_NAME} ${MQ_COMP_DEPENDS})
    endif()

    if(MQ_COMP_LIBS)
        target_link_libraries(${COMP_NAME} ${MQ_COMP_LIBS})
    endif()

    set_target_properties(${COMP_NAME} PROPERTIES
        OUTPUT_NAME "${COMP_NAME}"
        FOLDER "Plugins/${PLUGIN_NAME}"
    )

    set(PLUGIN_STAGE_DIR "${CMAKE_BINARY_DIR}/Plugins/${PLUGIN_NAME}")
    set(EDITOR_PLUGIN_STAGE_DIR "${CMAKE_BINARY_DIR}/Applications/Editor/Plugins/${PLUGIN_NAME}")
    set(GAME_PLUGIN_STAGE_DIR "${CMAKE_BINARY_DIR}/Applications/Game/Plugins/${PLUGIN_NAME}")
    set(CLIEDITOR_PLUGIN_STAGE_DIR "${CMAKE_BINARY_DIR}/Applications/CliEditor/Plugins/${PLUGIN_NAME}")
    set(EDITOR_BIN_DIR "${CMAKE_BINARY_DIR}/Applications/Editor")
    set(GAME_BIN_DIR "${CMAKE_BINARY_DIR}/Applications/Game")
    set(CLIEDITOR_BIN_DIR "${CMAKE_BINARY_DIR}/Applications/CliEditor")

    add_custom_command(TARGET ${COMP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${EDITOR_PLUGIN_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GAME_PLUGIN_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CLIEDITOR_PLUGIN_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${EDITOR_BIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GAME_BIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CLIEDITOR_BIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${COMP_NAME}>" "${PLUGIN_STAGE_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${COMP_NAME}>" "${EDITOR_PLUGIN_STAGE_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${COMP_NAME}>" "${GAME_PLUGIN_STAGE_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${COMP_NAME}>" "${CLIEDITOR_PLUGIN_STAGE_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${COMP_NAME}>" "${EDITOR_BIN_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${COMP_NAME}>" "${GAME_BIN_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${COMP_NAME}>" "${CLIEDITOR_BIN_DIR}/"
        COMMENT "Staging Composite DLL ${COMP_NAME}.dll for Plugin ${PLUGIN_NAME}"
    )

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugin.mq")
        set(_stage_mq_target "${PLUGIN_NAME}_stage_plugin_mq")
        if(NOT TARGET ${_stage_mq_target})
            add_custom_target(${_stage_mq_target} ALL
                COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_STAGE_DIR}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${EDITOR_PLUGIN_STAGE_DIR}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${GAME_PLUGIN_STAGE_DIR}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CLIEDITOR_PLUGIN_STAGE_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/plugin.mq" "${PLUGIN_STAGE_DIR}/plugin.mq"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/plugin.mq" "${EDITOR_PLUGIN_STAGE_DIR}/plugin.mq"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/plugin.mq" "${GAME_PLUGIN_STAGE_DIR}/plugin.mq"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/plugin.mq" "${CLIEDITOR_PLUGIN_STAGE_DIR}/plugin.mq"
                COMMENT "Staging plugin.mq for Plugin ${PLUGIN_NAME}"
            )
        endif()
        add_dependencies(${COMP_NAME} ${_stage_mq_target})
    endif()
endfunction()

# ==============================================================================
# mq_add_plugin(<PluginName>
#     [BUILTIN]
#     [VERSION "1.0.0"]
#     COMPOSITES <Comp1> <Comp2> ...
# )
# Plugin 是逻辑目录分组，不生成单一聚合 DLL，仅聚合所辖 Composite 目标并映射头文件
# ==============================================================================
function(mq_add_plugin PLUGIN_FIRST_ARG)
    set(options BUILTIN)
    set(oneValueArgs NAME VERSION)
    set(multiValueArgs COMPOSITES)
    cmake_parse_arguments(MQ_PLUGIN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(MQ_PLUGIN_NAME)
        set(PLUGIN_NAME "${MQ_PLUGIN_NAME}")
    else()
        set(PLUGIN_NAME "${PLUGIN_FIRST_ARG}")
    endif()

    map_target_headers(
        ${PLUGIN_NAME}_HeaderMap
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        INCLUDE_PREFIX "${PLUGIN_NAME}"
    )

    add_library(${PLUGIN_NAME} INTERFACE)

    target_include_directories(${PLUGIN_NAME} INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    if(MQ_PLUGIN_COMPOSITES)
        add_dependencies(${PLUGIN_NAME} ${MQ_PLUGIN_COMPOSITES})
        target_link_libraries(${PLUGIN_NAME} INTERFACE ${MQ_PLUGIN_COMPOSITES})
    endif()
endfunction()
