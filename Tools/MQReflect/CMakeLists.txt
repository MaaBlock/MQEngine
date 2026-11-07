# Tools/MQReflect/CMakeLists.txt
cmake_minimum_required(VERSION 3.22.1)

set(MQ_REFLECT_SOURCES
    src/Lexer.cpp
    src/Parser.cpp
    src/Filter.cpp
    src/Generator.cpp
    src/main.cpp
)

set(MQ_REFLECT_HEADERS
    src/Token.h
    src/AST.h
    src/Lexer.h
    src/Parser.h
    src/Filter.h
    src/Generator.h
)

add_executable(mq_reflect ${MQ_REFLECT_SOURCES} ${MQ_REFLECT_HEADERS})

target_include_directories(mq_reflect PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

set_target_properties(mq_reflect PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(MSVC)
    target_compile_options(mq_reflect PRIVATE /W4 /utf-8)
else()
    target_compile_options(mq_reflect PRIVATE -Wall -Wextra)
endif()
