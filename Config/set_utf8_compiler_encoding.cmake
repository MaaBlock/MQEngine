function(set_utf8_for_target target_name)
    if(MSVC) # msvc
        target_compile_options(${target_name} PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
                $<$<COMPILE_LANGUAGE:C>:/utf-8>
                $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8>
        )
    else() # gcc 或 clang
        target_compile_options(${target_name} PRIVATE
                -finput-charset=UTF-8     # 输入编码
                -fexec-charset=UTF-8      # 执行编码
        )
    endif()
endfunction()