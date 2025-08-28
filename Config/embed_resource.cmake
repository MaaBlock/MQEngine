
#
# 定义一个函数，该函数仅使用 CMake 将二进制文件嵌入到目标中。
#
# 用法:
# fct_embed_resource(TARGET <目标名称>
#                    FILES <文件1> <文件2> ...
#                    [PREFIX <符号前缀>])
function(fct_embed_resource)
    set(options)
    set(oneValueArgs TARGET PREFIX)
    set(multiValueArgs FILES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_TARGET)
        message(FATAL_ERROR "fct_embed_resource: 未指定 TARGET。")
    endif()
    if(NOT ARG_FILES)
        message(FATAL_ERROR "fct_embed_resource: 未指定 FILES。")
    endif()

    set(generated_sources "")

    foreach(resource_file ${ARG_FILES})
        get_filename_component(file_name ${resource_file} NAME_WE)
        get_filename_component(abs_file_path ${resource_file} ABSOLUTE)

        # 清理符号名称（将无效字符替换为 _）
        string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" symbol_name ${file_name})
        if(ARG_PREFIX)
            # 将前缀和符号名拼接，不带下划线
            set(symbol_name "${ARG_PREFIX}${symbol_name}")
        endif()

        set(generated_header "${CMAKE_CURRENT_BINARY_DIR}/generated_resources/${symbol_name}.h")

        # --- 开始：在配置时生成 C++ 数组内容 ---
        file(READ "${abs_file_path}" hex_content HEX)
        file(SIZE "${abs_file_path}" file_size)

        set(c_array_content "")
        set(byte_count 0)

        string(LENGTH "${hex_content}" hex_len)
        if(hex_len GREATER 0)
            math(EXPR max_idx "${hex_len} - 1")

            foreach(idx RANGE 0 ${max_idx} 2)
                string(SUBSTRING "${hex_content}" ${idx} 2 byte)

                if(${byte_count} EQUAL 0)
                    string(APPEND c_array_content "    ")
                endif()

                string(APPEND c_array_content "0x${byte}, ")
                math(EXPR byte_count "${byte_count} + 1")

                if(${byte_count} EQUAL 16)
                    string(APPEND c_array_content "\n")
                    set(byte_count 0)
                endif()
            endforeach()
        endif()

        # 如果文件不为空，则删除末尾的逗号和空格
        if(file_size GREATER 0)
            string(LENGTH "${c_array_content}" c_array_content_len)
            if(c_array_content_len GREATER 1)
                math(EXPR new_len "${c_array_content_len} - 2")
                string(SUBSTRING "${c_array_content}" 0 ${new_len} c_array_content)
            endif()
        endif()
        # --- 结束：生成 C++ 数组内容 ---

        # 为 file(GENERATE) 转义反斜杠和美元符号
        string(REPLACE "\\" "\\\\" c_array_content_escaped "${c_array_content}")
        string(REPLACE "$" "\\$" c_array_content_escaped "${c_array_content_escaped}")

        # C++ 头文件内容模板
        set(header_content_template "#pragma once

#include <cstddef>

const unsigned char ${symbol_name}[] = {
${c_array_content_escaped}
};

const size_t ${symbol_name}Size = ${file_size};
")

        # 使用 file(GENERATE) 在构建时写入头文件。
        # 这可以确保如果源资源文件发生更改，头文件会被重新生成。
        file(GENERATE
                OUTPUT ${generated_header}
                CONTENT "${header_content_template}"
        )

        list(APPEND generated_sources ${generated_header})
    endforeach()

    # 将生成的文件添加到目标的源文件中
    target_sources(${ARG_TARGET} PRIVATE ${generated_sources})

    # 将生成文件的目录添加到目标的包含路径中
    target_include_directories(${ARG_TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated_resources")
endfunction()