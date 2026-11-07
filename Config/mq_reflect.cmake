# Config/mq_reflect.cmake
# MQEngine native reflection preprocessor CMake integration

cmake_minimum_required(VERSION 3.22.1)

function(mq_target_reflect)
    set(options AUTO_REFLECT)
    set(oneValueArgs TARGET OUTPUT_NAME)
    set(multiValueArgs HEADERS DIRS)
    cmake_parse_arguments(MQ_REFLECT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT MQ_REFLECT_TARGET)
        message(FATAL_ERROR "mq_target_reflect: TARGET argument is required")
    endif()

    if(NOT TARGET ${MQ_REFLECT_TARGET})
        message(FATAL_ERROR "mq_target_reflect: Target '${MQ_REFLECT_TARGET}' does not exist")
    endif()

    if(NOT MQ_REFLECT_OUTPUT_NAME)
        set(MQ_REFLECT_OUTPUT_NAME "${MQ_REFLECT_TARGET}_reflection")
    endif()

    # 汇总输入头文件列表
    set(target_headers ${MQ_REFLECT_HEADERS})
    foreach(d ${MQ_REFLECT_DIRS})
        file(GLOB_RECURSE found_headers "${d}/*.h" "${d}/*.hpp")
        list(APPEND target_headers ${found_headers})
    endforeach()

    if(NOT target_headers)
        message(WARNING "mq_target_reflect: No headers supplied for target '${MQ_REFLECT_TARGET}'")
        return()
    endif()

    set(gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_reflection")
    set(gen_file "${gen_dir}/${MQ_REFLECT_OUTPUT_NAME}.gen.h")

    # 组装 CLI 参数
    set(tool_args)
    if(MQ_REFLECT_AUTO_REFLECT)
        list(APPEND tool_args "--auto-reflect")
    endif()
    list(APPEND tool_args "-o" "${gen_file}")
    list(APPEND tool_args ${target_headers})

    # 自定义构建命令（mq_reflect 内部具有 write-if-changed 内容对比能力）
    add_custom_command(
        OUTPUT "${gen_file}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${gen_dir}"
        COMMAND $<TARGET_FILE:mq_reflect> ${tool_args}
        DEPENDS mq_reflect ${target_headers}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Running mq_reflect: generating ${MQ_REFLECT_OUTPUT_NAME}.gen.h"
        VERBATIM
    )

    # 自定义目标保证在编译目标 C++ 源码前必须先生成 .gen.h
    set(custom_target_name "${MQ_REFLECT_TARGET}_reflect_gen")
    add_custom_target(${custom_target_name} DEPENDS "${gen_file}")
    add_dependencies(${MQ_REFLECT_TARGET} ${custom_target_name})

    # 将生成的反射头文件目录添加至目标的 include 路径
    target_include_directories(${MQ_REFLECT_TARGET} PUBLIC
        $<BUILD_INTERFACE:${gen_dir}>
    )

    # 抑制 MSVC 对标准 C++20 [[mq::...]] 属性未识别的 C5030 警告
    if(MSVC)
        target_compile_options(${MQ_REFLECT_TARGET} PRIVATE /wd5030)
    else()
        target_compile_options(${MQ_REFLECT_TARGET} PRIVATE -Wno-unknown-attributes)
    endif()

    message(STATUS "Configured mq_target_reflect for: ${MQ_REFLECT_TARGET} (${gen_file})")
endfunction()
