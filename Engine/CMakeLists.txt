file(GLOB CORE_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/core/*.cpp"
)

add_library(Engine SHARED
    ${CORE_MODULE_SOURCES}
)

target_link_fct_libraries(Engine)


function(target_link_engine target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Target ${target_name} does not exist")
    endif()

    target_link_libraries(${target_name} PRIVATE Engine)
    add_dependencies(${target_name} Engine)

    if(WIN32)
        add_custom_target(${target_name}_copy_engine_dll ALL
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Engine>
                $<TARGET_FILE_DIR:${target_name}>
                DEPENDS Engine
                COMMENT "Copying Engine.dll to ${target_name} directory"
        )

        add_dependencies(${target_name} ${target_name}_copy_engine_dll)
    endif()

    message(STATUS "Configured Engine for target: ${target_name}")
endfunction()