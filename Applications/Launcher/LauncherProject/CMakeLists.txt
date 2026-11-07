include("${CMAKE_SOURCE_DIR}/Config/copy_headers.cmake")
include("${CMAKE_SOURCE_DIR}/Config/set_utf8_compiler_encoding.cmake")

file(GLOB LAUNCHER_NATIVE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/*.cpp"
        "${CMAKE_SOURCE_DIR}/ThirdParty/tinyfiledialogs/tinyfiledialogs.c"
)

file(GLOB LAUNCHER_NATIVE_HEADERS
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/*.h"
        "./src/thirdparty/*.h"
)

if(ANDROID)
    set(LAUNCHER_NATIVE_LIB_TYPE STATIC)
else()
    set(LAUNCHER_NATIVE_LIB_TYPE SHARED)
endif()

add_library(LauncherNative ${LAUNCHER_NATIVE_LIB_TYPE}
        ${LAUNCHER_NATIVE_SOURCES}
        ${LAUNCHER_NATIVE_HEADERS}
)

target_compile_definitions(LauncherNative PRIVATE BUILD_LAUNCHER_NATIVE)

add_copy_target_headers(LauncherNative "${LAUNCHER_NATIVE_HEADERS}")

target_include_directories(LauncherNative PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        "${CMAKE_SOURCE_DIR}/ThirdParty/tinyfiledialogs"
)

target_link_fct_libraries(LauncherNative)
target_link_fct_node_libraries(LauncherNative)
target_link_fct_nbr_libraries(LauncherNative)
target_link_engine(LauncherNative)
if(COMMAND target_link_imgui_plugin)
    target_link_imgui_plugin(LauncherNative)
endif()
set_utf8_for_target(LauncherNative)

function(target_link_launcher_native target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Target ${target_name} does not exist")
    endif()

    if(ANDROID)
        target_link_libraries(${target_name} LauncherNative)
    endif()
    add_dependencies(${target_name} LauncherNative)

    if(WIN32)
        add_custom_target(${target_name}_copy_launcher_native_dll ALL
                COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target_name}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:LauncherNative>
                $<TARGET_FILE_DIR:${target_name}>
                DEPENDS LauncherNative
                COMMENT "Copying LauncherNative.dll to ${target_name} directory"
        )
        add_dependencies(${target_name} ${target_name}_copy_launcher_native_dll)
    endif()

    message(STATUS "Configured LauncherNative for target: ${target_name}")
endfunction()
