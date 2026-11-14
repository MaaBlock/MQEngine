add_subdirectory(Core)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Plugins/CMakeLists.txt")
    add_subdirectory(Plugins)
endif()
