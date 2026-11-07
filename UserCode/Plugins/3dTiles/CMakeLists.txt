include("${CMAKE_SOURCE_DIR}/Config/copy_headers.cmake")
include("${CMAKE_SOURCE_DIR}/Config/set_utf8_compiler_encoding.cmake")
include(FetchContent)

find_package(OpenSSL REQUIRED)
find_package(spdlog CONFIG REQUIRED)

set(CESIUM_TESTS_ENABLED OFF CACHE BOOL "" FORCE)
set(CESIUM_USE_SYSTEM_SQLITE3 ON CACHE BOOL "" FORCE)
set(CESIUM_USE_SYSTEM_URIPARSER ON CACHE BOOL "" FORCE)
set(CESIUM_USE_SYSTEM_DRACO ON CACHE BOOL "" FORCE)
set(CESIUM_USE_SYSTEM_RAPIDJSON OFF CACHE BOOL "" FORCE)
set(GLM_ENABLE_CXX_20 OFF CACHE BOOL "" FORCE)

if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_CESIUM-NATIVE AND EXISTS "G:/nexus_workspace/NexusEngine/build-windows-debug/_deps/cesium-native-src/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_CESIUM-NATIVE "G:/nexus_workspace/NexusEngine/build-windows-debug/_deps/cesium-native-src" CACHE PATH "Local cesium-native source cache")
endif()

FetchContent_Declare(
    cesium-native
    GIT_REPOSITORY https://github.com/CesiumGS/cesium-native.git
    GIT_TAG        v0.35.0
    PATCH_COMMAND  ${CMAKE_COMMAND} -DCESIUM_SRC_DIR=<SOURCE_DIR> -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_cesium.cmake"
)

set(_3DTILES_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_C_COMPILER_WORKS TRUE)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM "3.5")
endif()
FetchContent_MakeAvailable(cesium-native)
set(BUILD_SHARED_LIBS ${_3DTILES_SAVED_BUILD_SHARED_LIBS})

if(DEFINED cesium-native_SOURCE_DIR AND EXISTS "${cesium-native_SOURCE_DIR}/CMakeLists.txt")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DCESIUM_SRC_DIR=${cesium-native_SOURCE_DIR} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_cesium.cmake"
    )
endif()

collect_module_files(TILES_3D_PLUGIN "./src")

if(ANDROID)
    set(TILES_3D_PLUGIN_LIB_TYPE STATIC)
else()
    set(TILES_3D_PLUGIN_LIB_TYPE SHARED)
endif()

add_library(3dTiles ${TILES_3D_PLUGIN_LIB_TYPE}
        ${TILES_3D_PLUGIN_MODULE_SOURCES}
        ${TILES_3D_PLUGIN_MODULE_HEADERS}
)

target_compile_definitions(3dTiles PRIVATE
        BUILD_3DTILES_PLUGIN
        CPPHTTPLIB_OPENSSL_SUPPORT
)

add_copy_target_headers(3dTiles "${TILES_3D_PLUGIN_MODULE_HEADERS}")

target_link_libraries(3dTiles
        httplib::httplib
        OpenSSL::SSL
        OpenSSL::Crypto
        Cesium3DTilesSelection
        Cesium3DTilesContent
        CesiumGeospatial
        CesiumGeometry
        CesiumGltf
        CesiumGltfContent
        CesiumGltfReader
        CesiumAsync
        CesiumUtility
        meshoptimizer
)
target_link_fct_libraries(3dTiles)
target_link_fct_node_libraries(3dTiles)
target_link_engine(3dTiles)
set_utf8_for_target(3dTiles)

target_include_directories(3dTiles PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_include_directories(3dTiles PRIVATE
        "${cesium-native_SOURCE_DIR}/extern/meshoptimizer/src"
)

if(WIN32 AND TARGET uriparser)
    add_custom_command(TARGET 3dTiles POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:uriparser>
            $<TARGET_FILE_DIR:3dTiles>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:uriparser>/$<TARGET_FILE_BASE_NAME:uriparser>.dll"
            $<TARGET_FILE_DIR:3dTiles>
        COMMENT "Copying uriparser DLL to 3dTiles plugin directory"
    )
endif()

function(target_link_3dtiles_plugin target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Target ${target_name} does not exist")
    endif()

    target_link_libraries(${target_name} 3dTiles)
    add_dependencies(${target_name} 3dTiles)

    if(WIN32)
        set(_3DTILES_COPY_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target_name}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:3dTiles>
                $<TARGET_FILE_DIR:${target_name}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE_DIR:3dTiles>/libssl-3-x64.dll"
                "$<TARGET_FILE_DIR:3dTiles>/libcrypto-3-x64.dll"
                $<TARGET_FILE_DIR:${target_name}>
        )
        if(TARGET uriparser)
            list(APPEND _3DTILES_COPY_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:uriparser>
                    $<TARGET_FILE_DIR:${target_name}>
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE_DIR:uriparser>/$<TARGET_FILE_BASE_NAME:uriparser>.dll"
                    $<TARGET_FILE_DIR:${target_name}>
            )
        endif()

        add_custom_target(${target_name}_copy_3dtiles_plugin_dll ALL
                ${_3DTILES_COPY_COMMANDS}
                DEPENDS 3dTiles
                COMMENT "Copying 3dTiles.dll, OpenSSL and uriparser DLLs to ${target_name} directory"
        )
        add_dependencies(${target_name} ${target_name}_copy_3dtiles_plugin_dll)
        if(TARGET uriparser)
            add_dependencies(${target_name}_copy_3dtiles_plugin_dll uriparser)
        endif()
    endif()

    message(STATUS "Configured 3dTiles plugin for target: ${target_name}")
endfunction()
