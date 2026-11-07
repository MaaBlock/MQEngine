include("${CMAKE_SOURCE_DIR}/Config/copy_headers.cmake")
include("${CMAKE_SOURCE_DIR}/Config/set_utf8_compiler_encoding.cmake")

collect_module_files(BASE       "./src/base")
collect_module_files(PLATFORM   "./src/platform")
collect_module_files(RESOURCE   "./src/resource")
collect_module_files(SCENE      "./src/scene")
collect_module_files(RENDER     "./src/render")
collect_module_files(EFFECT     "./src/effect")
collect_module_files(ANIMATION  "./src/animation")
collect_module_files(PHYSICS    "./src/physics")
collect_module_files(SCRIPT     "./src/script")
collect_module_files(NETWORK    "./src/network")
collect_module_files(HID        "./src/hid")
collect_module_files(RUNTIME    "./src/runtime")
collect_module_files(THIRDPARTY "./src/thirdparty")
collect_module_files(ALL_SRC    "./src")

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

set(ALL_HEADERS
        ${ENGINE_HEADERS}
)

add_copy_target_headers(Engine "${ALL_HEADERS}")


find_package(spdlog CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(ctre CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_path(TOMLPLUSPLUS_INCLUDE_DIRS "toml++/toml.hpp")
find_path(FLATBUFFERS_INCLUDE_DIRS "flatbuffers/flatbuffers.h")
target_link_libraries(Engine
        spdlog::spdlog
        absl::any absl::log absl::base absl::bits
        absl::flags absl::flags_parse absl::flags_reflection
        ctre::ctre
        fmt::fmt
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
target_link_fct_sai_libraries(Engine)
target_link_fct_node_libraries(Engine)
target_link_fct_nbr_libraries(Engine)
if(NOT ANDROID)
    target_link_fct_pybri_libraries(Engine)
endif()
target_link_fct_imgui_libraries(Engine)
set_utf8_for_target(Engine)

target_include_directories(Engine PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

function(target_link_engine target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Target ${target_name} does not exist")
    endif()

    target_link_libraries(${target_name} Engine)
    if(NOT ANDROID)
        target_link_fct_pybri_libraries(${target_name})
    endif()

    add_dependencies(${target_name} Engine)

    if(WIN32)
        add_custom_target(${target_name}_copy_engine_dll ALL
                COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target_name}>
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

fct_embed_resource(
        TARGET Engine
        FILES "${CMAKE_SOURCE_DIR}/Assets/Shaders/ObjectPixel.hlsl"
        PREFIX "g_engineShader"
)
fct_embed_resource(
        TARGET Engine
        FILES "${CMAKE_SOURCE_DIR}/Assets/Shaders/ObjectVertex.hlsl"
        PREFIX "g_engineShader"
)
fct_embed_resource(
        TARGET Engine
        FILES "${CMAKE_SOURCE_DIR}/Assets/Shaders/ShadowVertex.hlsl"
        PREFIX "g_engineShader"
)
fct_embed_resource(
        TARGET Engine
        FILES "${CMAKE_SOURCE_DIR}/Assets/Shaders/DiffuseObjectVertex.hlsl"
        PREFIX "g_engineShader"
)
fct_embed_resource(
        TARGET Engine
        FILES "${CMAKE_SOURCE_DIR}/Assets/Shaders/DiffuseObjectPixel.hlsl"
        PREFIX "g_engineShader"
)
fct_embed_resource(
        TARGET Engine
        FILES "${CMAKE_SOURCE_DIR}/Assets/Shaders/NormalMapObjectVertex.hlsl"
        PREFIX "g_engineShader"
)
fct_embed_resource(
        TARGET Engine
        FILES "${CMAKE_SOURCE_DIR}/Assets/Shaders/NormalMapObjectPixel.hlsl"
        PREFIX "g_engineShader"
)
fct_embed_resource(
        TARGET Engine
        FILES "${CMAKE_SOURCE_DIR}/Assets/Shaders/PBRVertex.hlsl"
        PREFIX "g_engineShader"
)
fct_embed_resource(
        TARGET Engine
        FILES "${CMAKE_SOURCE_DIR}/Assets/Shaders/PBRPixel.hlsl"
        PREFIX "g_engineShader"
)
fct_embed_resource(
        TARGET Engine
        FILES "${CMAKE_SOURCE_DIR}/Assets/Shaders/SkyboxVertex.hlsl"
        PREFIX "g_engineShader"
)
fct_embed_resource(
        TARGET Engine
        FILES "${CMAKE_SOURCE_DIR}/Assets/Shaders/SkyboxPixel.hlsl"
        PREFIX "g_engineShader"
)
fct_embed_resource(
        TARGET Engine
        FILES "${CMAKE_SOURCE_DIR}/Assets/Shaders/ToneMappingVertex.hlsl"
        PREFIX "g_engineShader"
)
fct_embed_resource(
        TARGET Engine
        FILES "${CMAKE_SOURCE_DIR}/Assets/Shaders/ToneMappingPixel.hlsl"
        PREFIX "g_engineShader"
)
fct_embed_resource(
        TARGET Engine
        FILES "${CMAKE_SOURCE_DIR}/Assets/Shaders/DeferredLightingVertex.hlsl"
        PREFIX "g_engineShader"
)
fct_embed_resource(
        TARGET Engine
        FILES "${CMAKE_SOURCE_DIR}/Assets/Shaders/DeferredLightingPixel.hlsl"
        PREFIX "g_engineShader"
)

include("${CMAKE_SOURCE_DIR}/Config/mq_reflect.cmake")

mq_target_reflect(
    TARGET Engine
    AUTO_REFLECT
    OUTPUT_NAME "Engine_reflection"
    HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/core/Engine.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/core/Runtime.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/scene/manager/DataManager.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/base/Foundation.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/base/MemoryManager.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/base/JobSystem.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/network/manager/NetworkManager.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/network/system/NetworkSyncSystem.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/animation/system/SkeletalAnimationSystem.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/hid/system/InputSystem.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/scene/system/ComponentReflection.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/physics/system/PhysicsSystem.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/scene/data/Component.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/hid/data/KeyCode.h"
)
