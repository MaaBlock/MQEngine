if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    set(_mq_launcher_src "${CMAKE_CURRENT_LIST_DIR}/msvc_utf8_launcher.cpp")
    set(_mq_launcher_exe "${CMAKE_BINARY_DIR}/CMakeFiles/msvc_utf8_launcher.exe")
    set(_mq_launcher_obj "${CMAKE_BINARY_DIR}/CMakeFiles/msvc_utf8_launcher.obj")
    if(NOT EXISTS "${_mq_launcher_exe}" OR "${_mq_launcher_src}" IS_NEWER_THAN "${_mq_launcher_exe}")
        set(_mq_launcher_tmp "${CMAKE_BINARY_DIR}/CMakeFiles/msvc_utf8_launcher_new.exe")
        set(_mq_launcher_old "${CMAKE_BINARY_DIR}/CMakeFiles/msvc_utf8_launcher_old.exe")
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" /nologo /O2 /EHsc /std:c++20 /utf-8
                    "${_mq_launcher_src}"
                    "/Fo${_mq_launcher_obj}"
                    "/Fe${_mq_launcher_tmp}"
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(EXISTS "${_mq_launcher_tmp}")
            if(EXISTS "${_mq_launcher_exe}")
                file(REMOVE "${_mq_launcher_old}")
                file(RENAME "${_mq_launcher_exe}" "${_mq_launcher_old}")
            endif()
            file(RENAME "${_mq_launcher_tmp}" "${_mq_launcher_exe}")
            file(REMOVE "${_mq_launcher_old}")
        endif()
    endif()
    if(EXISTS "${_mq_launcher_exe}")
        file(TO_CMAKE_PATH "${_mq_launcher_exe}" _mq_launcher_path)
        set(CMAKE_C_COMPILER_LAUNCHER "${_mq_launcher_path}" CACHE FILEPATH "MSVC UTF-8 diagnostic launcher" FORCE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${_mq_launcher_path}" CACHE FILEPATH "MSVC UTF-8 diagnostic launcher" FORCE)
        foreach(_mq_rule_var
                CMAKE_CXX_SCANDEP_SOURCE
                CMAKE_C_CREATE_SHARED_LIBRARY
                CMAKE_CXX_CREATE_SHARED_LIBRARY
                CMAKE_C_CREATE_SHARED_MODULE
                CMAKE_CXX_CREATE_SHARED_MODULE
                CMAKE_C_LINK_EXECUTABLE
                CMAKE_CXX_LINK_EXECUTABLE
                CMAKE_C_CREATE_STATIC_LIBRARY
                CMAKE_CXX_CREATE_STATIC_LIBRARY)
            if(DEFINED ${_mq_rule_var} AND NOT "${${_mq_rule_var}}" MATCHES "msvc_utf8_launcher")
                set(${_mq_rule_var} "\"${_mq_launcher_path}\" ${${_mq_rule_var}}")
            endif()
        endforeach()
    endif()
endif()

function(set_utf8_for_target target_name)
    if(MSVC) # msvc
        target_compile_options(${target_name} PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
                $<$<COMPILE_LANGUAGE:C>:/utf-8>
                $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8>
        )
        if(CMAKE_GENERATOR MATCHES "Ninja" AND EXISTS "${CMAKE_BINARY_DIR}/CMakeFiles/msvc_utf8_launcher.exe")
            file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/CMakeFiles/msvc_utf8_launcher.exe" _mq_target_launcher)
            set_target_properties(${target_name} PROPERTIES
                C_COMPILER_LAUNCHER "${_mq_target_launcher}"
                CXX_COMPILER_LAUNCHER "${_mq_target_launcher}"
            )
        endif()
    else() # gcc 或 clang
        target_compile_options(${target_name} PRIVATE
                -finput-charset=UTF-8     # 输入编码
                -fexec-charset=UTF-8      # 执行编码
        )
    endif()
endfunction()
