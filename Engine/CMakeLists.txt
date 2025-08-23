include("../Config/copy_headers.cmake")

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
file(GLOB DATA_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/data/*.cpp"
)
file(GLOB DATA_MODULE_HEADERS
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/data/*.h"
        "./src/data/*.hpp"
)

file(GLOB THIRDPARTY_MODULE_HEADERS
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/thirdparty/*.h"
)

add_library(Engine SHARED
    ${CORE_MODULE_SOURCES}
    ${CORE_MODULE_HEADERS}
    ${DATA_MODULE_SOURCES}
    ${DATA_MODULE_HEADERS}
    ${THIRDPARTY_MODULE_HEADERS}
)
target_compile_definitions(Engine PRIVATE BUILD_ENGINE)

set(ALL_HEADERS
        ${CORE_MODULE_HEADERS}
        ${THIRDPARTY_MODULE_HEADERS}
        "./src/headers.h"
)

add_copy_target_headers(Engine "${ALL_HEADERS}")

target_link_fct_libraries(Engine)
target_link_fct_node_libraries(Engine)
target_link_fct_imgui_libraries(Engine)

target_include_directories(Engine PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

function(target_link_engine target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Target ${target_name} does not exist")
    endif()

    target_link_libraries(${target_name} Engine)

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