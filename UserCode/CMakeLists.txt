if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Plugins/CMakeLists.txt")
    add_subdirectory(Plugins)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Native/CMakeLists.txt")
    add_subdirectory(Native)
elseif(NOT TARGET UserCode AND EXISTS "${CMAKE_SOURCE_DIR}/Templates/Native/SampleNativeScript/CMakeLists.txt")
    add_subdirectory("${CMAKE_SOURCE_DIR}/Templates/Native/SampleNativeScript" "${CMAKE_CURRENT_BINARY_DIR}/SampleNativeScript")
endif()
