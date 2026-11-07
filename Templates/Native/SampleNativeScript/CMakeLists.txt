include("${CMAKE_SOURCE_DIR}/Config/copy_headers.cmake")
include("${CMAKE_SOURCE_DIR}/Config/set_utf8_compiler_encoding.cmake")

file(GLOB USER_CODE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/*.cpp"
)

file(GLOB USER_CODE_HEADERS
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/*.h"
        "./src/thirdparty/*.h"
)

if(ANDROID)
    set(USER_CODE_LIB_TYPE STATIC)
else()
    set(USER_CODE_LIB_TYPE SHARED)
endif()

add_library(UserCode ${USER_CODE_LIB_TYPE}
        ${USER_CODE_SOURCES}
        ${USER_CODE_HEADERS}
)

target_compile_definitions(UserCode PRIVATE BUILD_USER_CODE)

add_copy_target_headers(UserCode "${USER_CODE_HEADERS}")

target_link_fct_libraries(UserCode)
target_link_fct_node_libraries(UserCode)
target_link_fct_nbr_libraries(UserCode)
target_link_fct_imgui_libraries(UserCode)
target_link_engine(UserCode)
target_link_imgui_plugin(UserCode)
set_utf8_for_target(UserCode)

target_include_directories(UserCode PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

function(target_link_user_code target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Target ${target_name} does not exist")
    endif()

    if(ANDROID)
        target_link_libraries(${target_name} UserCode)
    endif()
    add_dependencies(${target_name} UserCode)

    if(WIN32)
        add_custom_target(${target_name}_copy_user_code_dll ALL
                COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target_name}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:UserCode>
                $<TARGET_FILE_DIR:${target_name}>
                DEPENDS UserCode
                COMMENT "Copying UserCode.dll to ${target_name} directory"
        )
        add_dependencies(${target_name} ${target_name}_copy_user_code_dll)
    endif()

    message(STATUS "Configured UserCode for target: ${target_name}")
endfunction()
