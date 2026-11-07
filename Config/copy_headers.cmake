function(add_copy_target_headers_custom_src target_name headers_list src_dir)
    set(copy_target_name "copy_${target_name}_headers")
    get_filename_component(src_dir "${src_dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

    set(include_base_dir "${CMAKE_CURRENT_SOURCE_DIR}/include/${target_name}")

    # 自动清理 include/<target_name> 下因 src 目录重构/移动而残留的旧头文件
    if(EXISTS "${include_base_dir}")
        file(GLOB_RECURSE _existing_inc_hdrs
            LIST_DIRECTORIES false
            "${include_base_dir}/*.h"
            "${include_base_dir}/*.hpp"
            "${include_base_dir}/*.inl"
        )
        foreach(_inc_hdr ${_existing_inc_hdrs})
            file(RELATIVE_PATH _inc_rel "${include_base_dir}" "${_inc_hdr}")
            if(NOT EXISTS "${src_dir}/${_inc_rel}")
                file(REMOVE "${_inc_hdr}")
            endif()
        endforeach()
    endif()

    file(MAKE_DIRECTORY "${include_base_dir}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include")

    set(copied_headers "")

    foreach(header_file ${headers_list})
        get_filename_component(header_file "${header_file}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

        file(RELATIVE_PATH relative_path "${src_dir}" "${header_file}")
        set(output_file "${include_base_dir}/${relative_path}")
        get_filename_component(output_dir "${output_file}" DIRECTORY)

        file(MAKE_DIRECTORY "${output_dir}")

        add_custom_command(
                OUTPUT "${output_file}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${header_file}"
                "${output_file}"
                DEPENDS "${header_file}"
                COMMENT "Copying: ${relative_path}"
        )

        list(APPEND copied_headers "${output_file}")
    endforeach()

    set(main_header_file "${src_dir}/${target_name}.h")
    if(EXISTS "${main_header_file}")
        set(main_output_file "${CMAKE_CURRENT_SOURCE_DIR}/include/${target_name}.h")
        add_custom_command(
                OUTPUT "${main_output_file}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${main_header_file}"
                "${main_output_file}"
                DEPENDS "${main_header_file}"
                COMMENT "Copying main header: ${target_name}.h"
        )
        list(APPEND copied_headers "${main_output_file}")
    endif()

    add_custom_target(${copy_target_name} ALL
            DEPENDS ${copied_headers}
            COMMENT "Headers for ${target_name} are up to date"
    )

    if(TARGET ${target_name})
        add_dependencies(${target_name} ${copy_target_name})
    endif()

    message(STATUS "Added copy headers target for: ${target_name}")
endfunction()

function(add_copy_target_headers target_name headers_list)
    add_copy_target_headers_custom_src(${target_name} "${headers_list}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
endfunction()

# ------------------------------------------------------------------------------
# 递归收集单个模块目录（含所有子目录）的源文件 (.cpp/.c/.cc) 与头文件 (.h/.hpp/.inl)
# 用法 1（生成 <PREFIX>_MODULE_SOURCES 与 <PREFIX>_MODULE_HEADERS 变量）：
#   collect_module_files(RENDER "./src/render")
# 用法 2（同时自动追加到总列表变量中）：
#   collect_module_files(RENDER "./src/render" APPEND_SOURCES ENGINE_SOURCES APPEND_HEADERS ENGINE_HEADERS)
# ------------------------------------------------------------------------------
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

# ------------------------------------------------------------------------------
# 批量递归收集多个子模块目录的源文件与头文件
# 用法：
#   collect_modules_recursive(ENGINE_SOURCES ENGINE_HEADERS
#       "./src/core"
#       "./src/data"
#       "./src/render"
#       ...
#   )
# ------------------------------------------------------------------------------
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
