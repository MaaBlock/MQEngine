# ------------------------------------------------------------------------------
# 将模块的 src 目录直接映射（Windows NTFS 目录联接 Junction / Unix 符号链接）到 include/<INCLUDE_PREFIX>
# 同时兼容源码开发模式与无源码 SDK 二进制分发模式：
# 1. 源码开发模式：include/MQEngine 直接通过 NTFS Junction / Symlink 映射到 Core/src，无需 copy headers；
# 2. 引擎内部与外部（Application / Native / Plugin / SDK）统一使用 #include <MQEngine/xxx/xxx.h>；
# 3. SDK 导出安装：cmake --install 时将 src/*.h 按目录结构安装至 <SDK>/include/MQEngine/。
# ------------------------------------------------------------------------------
function(map_target_headers target_name src_dir)
    set(options "")
    set(oneValueArgs INCLUDE_PREFIX)
    set(multiValueArgs EXTRA_PREFIXES)
    cmake_parse_arguments(MAP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT MAP_INCLUDE_PREFIX)
        set(MAP_INCLUDE_PREFIX "${target_name}")
    endif()

    get_filename_component(abs_src_dir "${src_dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

    # 自动清理源码树下遗留的旧 include 目录与软链接/Junction，防止污染 CLion 源码索引与重命名
    set(legacy_source_include_dir "${CMAKE_CURRENT_SOURCE_DIR}/include")
    if(EXISTS "${legacy_source_include_dir}")
        file(GLOB _legacy_entries "${legacy_source_include_dir}/*")
        foreach(_legacy_item ${_legacy_entries})
            if(IS_DIRECTORY "${_legacy_item}" OR IS_SYMLINK "${_legacy_item}")
                if(WIN32)
                    file(TO_NATIVE_PATH "${_legacy_item}" _native_legacy_item)
                    execute_process(
                        COMMAND cmd /c rmdir "${_native_legacy_item}"
                        OUTPUT_QUIET ERROR_QUIET
                    )
                endif()
            endif()
        endforeach()
        file(REMOVE_RECURSE "${legacy_source_include_dir}")
    endif()

    # 将头文件软链接映射目录统一放入构建目录（CLion 默认 Excluded，不污染源码树）
    set(include_root_dir "${CMAKE_BINARY_DIR}/generated-include")
    file(MAKE_DIRECTORY "${include_root_dir}")

    set(all_prefixes "${MAP_INCLUDE_PREFIX}" ${MAP_EXTRA_PREFIXES})
    if(EXISTS "${abs_src_dir}")
        foreach(prefix ${all_prefixes})
            set(link_path "${include_root_dir}/${prefix}")

            if(NOT EXISTS "${link_path}")
                # 使用 CMake 原生 SYMBOLIC（即 mklink /D 真符号链接，clangd realpath 能识别指向 src/）
                file(CREATE_LINK "${abs_src_dir}" "${link_path}" SYMBOLIC RESULT _link_res)
                if(NOT "${_link_res}" STREQUAL "0" AND WIN32)
                    file(TO_NATIVE_PATH "${link_path}" _native_link)
                    file(TO_NATIVE_PATH "${abs_src_dir}" _native_src)
                    execute_process(
                        COMMAND cmd /c mklink /D "${_native_link}" "${_native_src}"
                        RESULT_VARIABLE _mklink_d_res
                        OUTPUT_QUIET ERROR_QUIET
                    )
                    if(NOT _mklink_d_res EQUAL 0)
                        execute_process(
                            COMMAND cmd /c mklink /J "${_native_link}" "${_native_src}"
                            OUTPUT_QUIET ERROR_QUIET
                        )
                    endif()
                endif()
            endif()

            set(umbrella_header "${include_root_dir}/${prefix}.h")
            if(NOT EXISTS "${umbrella_header}")
                if(EXISTS "${abs_src_dir}/headers.h")
                    file(WRITE "${umbrella_header}" "#ifndef ${prefix}_UMBRELLA_H\n#define ${prefix}_UMBRELLA_H\n#include <${prefix}/headers.h>\n#endif\n")
                elseif(EXISTS "${abs_src_dir}/${target_name}.h")
                    file(WRITE "${umbrella_header}" "#ifndef ${prefix}_UMBRELLA_H\n#define ${prefix}_UMBRELLA_H\n#include <${prefix}/${target_name}.h>\n#endif\n")
                endif()
            endif()
        endforeach()
    endif()

    if(TARGET ${target_name})
        target_include_directories(${target_name}
            PUBLIC
                $<BUILD_INTERFACE:${include_root_dir}>
                $<INSTALL_INTERFACE:include>
        )
    endif()

    # 支持无源码 SDK 打包安装：安装时将头文件归档至 include/<INCLUDE_PREFIX>
    if(EXISTS "${abs_src_dir}")
        install(DIRECTORY "${abs_src_dir}/"
            DESTINATION "include/${MAP_INCLUDE_PREFIX}"
            FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.hpp"
            PATTERN "*.inl"
        )
    endif()

    message(STATUS "Mapped headers for ${target_name}: <${MAP_INCLUDE_PREFIX}/...> -> ${abs_src_dir}")
endfunction()

function(add_copy_target_headers_custom_src target_name headers_list src_dir)
    map_target_headers(${target_name} "${src_dir}" INCLUDE_PREFIX "${target_name}")
endfunction()

function(add_copy_target_headers target_name headers_list)
    if("${target_name}" STREQUAL "Engine")
        map_target_headers(${target_name} "${CMAKE_CURRENT_SOURCE_DIR}/src" INCLUDE_PREFIX "MQEngine")
    else()
        map_target_headers(${target_name} "${CMAKE_CURRENT_SOURCE_DIR}/src" INCLUDE_PREFIX "${target_name}")
    endif()
endfunction()

function(collect_module_files module_prefix module_dir)
    set(options "")
    set(oneValueArgs APPEND_SOURCES APPEND_HEADERS)
    set(multiValueArgs "")
    cmake_parse_arguments(MOD "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    get_filename_component(_abs_dir "${module_dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

    file(GLOB_RECURSE _mod_sources
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "${_abs_dir}/*.cpp"
        "${_abs_dir}/*.cc"
        "${_abs_dir}/*.c"
    )

    file(GLOB_RECURSE _mod_headers
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "${_abs_dir}/*.h"
        "${_abs_dir}/*.hpp"
        "${_abs_dir}/*.inl"
    )

    set(${module_prefix}_MODULE_SOURCES "${_mod_sources}" PARENT_SCOPE)
    set(${module_prefix}_MODULE_HEADERS "${_mod_headers}" PARENT_SCOPE)

    if(MOD_APPEND_SOURCES)
        set(_cur_sources "${${MOD_APPEND_SOURCES}}")
        list(APPEND _cur_sources ${_mod_sources})
        set(${MOD_APPEND_SOURCES} "${_cur_sources}" PARENT_SCOPE)
    endif()

    if(MOD_APPEND_HEADERS)
        set(_cur_headers "${${MOD_APPEND_HEADERS}}")
        list(APPEND _cur_headers ${_mod_headers})
        set(${MOD_APPEND_HEADERS} "${_cur_headers}" PARENT_SCOPE)
    endif()
endfunction()

function(collect_modules_recursive out_sources_var out_headers_var)
    set(_all_sources "${${out_sources_var}}")
    set(_all_headers "${${out_headers_var}}")

    foreach(_dir ${ARGN})
        get_filename_component(_abs_dir "${_dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

        file(GLOB_RECURSE _dir_sources
            LIST_DIRECTORIES false
            CONFIGURE_DEPENDS
            "${_abs_dir}/*.cpp"
            "${_abs_dir}/*.cc"
            "${_abs_dir}/*.c"
        )
        file(GLOB_RECURSE _dir_headers
            LIST_DIRECTORIES false
            CONFIGURE_DEPENDS
            "${_abs_dir}/*.h"
            "${_abs_dir}/*.hpp"
            "${_abs_dir}/*.inl"
        )

        list(APPEND _all_sources ${_dir_sources})
        list(APPEND _all_headers ${_dir_headers})
    endforeach()

    set(${out_sources_var} "${_all_sources}" PARENT_SCOPE)
    set(${out_headers_var} "${_all_headers}" PARENT_SCOPE)
endfunction()
