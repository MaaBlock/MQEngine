include("${CMAKE_SOURCE_DIR}/Config/copy_headers.cmake")
include("${CMAKE_SOURCE_DIR}/Config/set_utf8_compiler_encoding.cmake")
# Engine/Core CMake configuration
collect_module_files(ALL_SRC "./src")

set(ENGINE_HEADERS
    ${ALL_SRC_MODULE_HEADERS}
)
set(ENGINE_SOURCES
    ${ALL_SRC_MODULE_SOURCES}
)

if(MSVC)
    add_compile_options(/bigobj /EHsc)
endif()

if(ANDROID)
    set(ENGINE_LIB_TYPE STATIC)
else()
    set(ENGINE_LIB_TYPE SHARED)
endif()

add_library(Engine ${ENGINE_LIB_TYPE}
    ${ENGINE_HEADERS}
    ${ENGINE_SOURCES}
)
target_compile_definitions(Engine PRIVATE BUILD_ENGINE)
if(MSVC)
    target_compile_options(Engine PRIVATE $<$<CONFIG:Debug>:/Z7>)
    target_link_options(Engine PRIVATE $<$<CONFIG:Debug>:/DEBUG>)
endif()

# 统一将 Engine/Core/src 映射到 <MQEngine/...>，引擎内部与外部统一使用 #include <MQEngine/xxx/xxx.h>
map_target_headers(Engine "${CMAKE_CURRENT_SOURCE_DIR}/src"
    INCLUDE_PREFIX "MQEngine"
)

find_package(spdlog CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(ctre CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS json)
find_path(TOMLPLUSPLUS_INCLUDE_DIRS "toml++/toml.hpp")
find_path(FLATBUFFERS_INCLUDE_DIRS "flatbuffers/flatbuffers.h")

target_link_libraries(Engine
    spdlog::spdlog
    absl::any absl::log absl::base absl::bits
    absl::flags absl::flags_parse absl::flags_reflection
    ctre::ctre
    fmt::fmt
    Boost::json
)
if(WIN32)
    target_link_libraries(Engine ws2_32 mswsock)
endif()
if(TOMLPLUSPLUS_INCLUDE_DIRS)
    target_include_directories(Engine PUBLIC "${TOMLPLUSPLUS_INCLUDE_DIRS}")
endif()
if(FLATBUFFERS_INCLUDE_DIRS)
    target_include_directories(Engine PUBLIC "${FLATBUFFERS_INCLUDE_DIRS}")
endif()

target_link_fct_libraries(Engine)
target_link_fct_profiling_libraries(Engine)
target_link_fct_sai_libraries(Engine)
target_link_fct_node_libraries(Engine)
target_link_fct_nbr_libraries(Engine)
if(NOT ANDROID)
    target_link_fct_pybri_libraries(Engine)
endif()
target_link_fct_imgui_libraries(Engine)
set_utf8_for_target(Engine)

function(target_link_engine target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Target ${target_name} does not exist")
    endif()

    target_link_libraries(${target_name} Engine)
    target_link_fct_profiling_libraries(${target_name})
    if(NOT ANDROID)
        target_link_fct_pybri_libraries(${target_name})
    endif()

    add_dependencies(${target_name} Engine)

    if(WIN32)
        string(MAKE_C_IDENTIFIER "${CMAKE_CURRENT_BINARY_DIR}" _engine_copy_dir_id)
        set(_copy_target_name "copy_engine_dll_${_engine_copy_dir_id}")
        if(NOT TARGET ${_copy_target_name})
            add_custom_target(${_copy_target_name} ALL
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target_name}>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Engine>"
                "$<TARGET_FILE_DIR:${target_name}>"
                DEPENDS Engine
                COMMENT "Copying Engine.dll to ${CMAKE_CURRENT_BINARY_DIR}"
            )
        endif()
        add_dependencies(${target_name} ${_copy_target_name})
    endif()
endfunction()
