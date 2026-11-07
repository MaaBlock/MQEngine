# Config/MQPluginSDK.cmake
# MQEngine 插件统一构建 SDK：同时支持主工程内嵌构建 (In-Tree) 与单插件脱离根 CMake 独立构建 (Standalone)

cmake_minimum_required(VERSION 3.22.1)

include_guard(GLOBAL)

# 1. 自动推导 MQENGINE_ROOT_DIR（指向 MQEngine/MQEngine 源码根目录）
if(NOT DEFINED MQENGINE_ROOT_DIR)
    get_filename_component(MQENGINE_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

include("${MQENGINE_ROOT_DIR}/Config/copy_headers.cmake")
include("${MQENGINE_ROOT_DIR}/Config/set_utf8_compiler_encoding.cmake")

# 2. 判断当前是否处于单插件独立构建模式 (Standalone Mode)
if(TARGET Engine)
    set(MQ_PLUGIN_STANDALONE_BUILD OFF)
else()
    set(MQ_PLUGIN_STANDALONE_BUILD ON)
endif()

# 3. 若处于独立构建模式，自动定位预编译的引擎构建目录 (MQENGINE_BUILD_DIR) 并导入核心目标
if(MQ_PLUGIN_STANDALONE_BUILD)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    if(MSVC)
        add_compile_options(/bigobj /EHsc /wd4251 /wd4275)
    endif()

    if(NOT DEFINED MQENGINE_BUILD_DIR)
        if(EXISTS "${MQENGINE_ROOT_DIR}/Output/build-windows-debug/Engine/Core/Engine.lib" OR
           EXISTS "${MQENGINE_ROOT_DIR}/Output/build-windows-debug/Engine/Engine.lib")
            set(MQENGINE_BUILD_DIR "${MQENGINE_ROOT_DIR}/Output/build-windows-debug")
        elseif(EXISTS "${MQENGINE_ROOT_DIR}/Output/build-windows-release/Engine/Core/Engine.lib" OR
               EXISTS "${MQENGINE_ROOT_DIR}/Output/build-windows-release/Engine/Engine.lib")
            set(MQENGINE_BUILD_DIR "${MQENGINE_ROOT_DIR}/Output/build-windows-release")
        else()
            message(FATAL_ERROR "MQPluginSDK (Standalone): 未找到预编译的 Engine.lib，请通过 -DMQENGINE_BUILD_DIR=<path> 指定引擎构建目录")
        endif()
    endif()
    file(TO_CMAKE_PATH "${MQENGINE_BUILD_DIR}" MQENGINE_BUILD_DIR)
    message(STATUS "[MQPluginSDK] Standalone 独立插件编译模式，复用引擎构建产物目录: ${MQENGINE_BUILD_DIR}")

    set(MQ_VCPKG_INSTALLED_DIR "${MQENGINE_BUILD_DIR}/vcpkg_installed/x64-windows")

    # 导入 mq_reflect 预编译可执行工具
    if(NOT TARGET mq_reflect AND EXISTS "${MQENGINE_BUILD_DIR}/Tools/MQReflect/mq_reflect.exe")
        add_executable(mq_reflect IMPORTED GLOBAL)
        set_target_properties(mq_reflect PROPERTIES
            IMPORTED_LOCATION "${MQENGINE_BUILD_DIR}/Tools/MQReflect/mq_reflect.exe"
        )
    endif()

    # 构建 MQEngine::StandaloneSDK 接口目标，承载全套头文件、宏定义与基础库依赖
    if(NOT TARGET MQEngine::StandaloneSDK)
        add_library(MQEngine::StandaloneSDK INTERFACE IMPORTED GLOBAL)

        # 确保 Standalone 配置时 Engine/Core/src 下的最新头文件自动同步至 Engine/Core/include/Engine
        set(_engine_core_root "${MQENGINE_ROOT_DIR}/Engine/Core")
        if(NOT EXISTS "${_engine_core_root}/src" AND EXISTS "${MQENGINE_ROOT_DIR}/Engine/src")
            set(_engine_core_root "${MQENGINE_ROOT_DIR}/Engine")
        endif()
        if(EXISTS "${_engine_core_root}/src")
            if(EXISTS "${_engine_core_root}/include/Engine")
                file(GLOB_RECURSE _existing_eng_inc_hdrs
                    LIST_DIRECTORIES false
                    "${_engine_core_root}/include/Engine/*.h"
                    "${_engine_core_root}/include/Engine/*.hpp"
                    "${_engine_core_root}/include/Engine/*.inl"
                )
                foreach(_einc ${_existing_eng_inc_hdrs})
                    file(RELATIVE_PATH _erel_inc "${_engine_core_root}/include/Engine" "${_einc}")
                    if(NOT EXISTS "${_engine_core_root}/src/${_erel_inc}")
                        file(REMOVE "${_einc}")
                    endif()
                endforeach()
            endif()
            file(GLOB_RECURSE _engine_all_hdrs
                LIST_DIRECTORIES false
                "${_engine_core_root}/src/*.h"
                "${_engine_core_root}/src/*.hpp"
                "${_engine_core_root}/src/*.inl"
            )
            foreach(_ehdr ${_engine_all_hdrs})
                file(RELATIVE_PATH _erel "${_engine_core_root}/src" "${_ehdr}")
                configure_file("${_ehdr}" "${_engine_core_root}/include/Engine/${_erel}" COPYONLY)
            endforeach()
        endif()

        set(_mq_sdk_includes
            "${MQENGINE_ROOT_DIR}/Engine/Core/include"
            "${MQENGINE_ROOT_DIR}/Engine/include"
            "${MQENGINE_BUILD_DIR}/Engine/Core/generated_reflection"
            "${MQENGINE_BUILD_DIR}/Engine/generated_reflection"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_IMGUI/imgui"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_IMGUI/imgui/backends"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_IMGUI/imnodes"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_IMGUI/include"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_RHI/include"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_Base/include"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_Base/ThirdParty/tracy/public"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_RHI/ThirdParty/work_contract/src"
            "${MQENGINE_BUILD_DIR}/include-src/src"
            "${MQENGINE_BUILD_DIR}/_deps/include-src/src"
            "${MQENGINE_ROOT_DIR}/Output/build-windows-release/include-src/src"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_Node/node/src"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_Node/node/deps/v8/include"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_Node/node/deps/uv/include"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_Node/include"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_SAI/include"
            "${MQENGINE_ROOT_DIR}/FCT/NBR/include"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_PyBri/include"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_Compute/include"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_Compute/ThirdParty/LuisaCompute/include"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_Compute/ThirdParty/LuisaCompute/src/ext/spdlog/include"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_Compute/ThirdParty/LuisaCompute/src/ext/magic_enum/include"
            "${MQENGINE_ROOT_DIR}/ThirdParty/ImGuiColorTextEdit"
            "${MQ_VCPKG_INSTALLED_DIR}/include"
            "${MQ_VCPKG_INSTALLED_DIR}/include/python3.11"
            "${MQ_VCPKG_INSTALLED_DIR}/include/physx"
            "${MQ_VCPKG_INSTALLED_DIR}/include/spirv_cross"
        )
        set(_mq_valid_includes "")
        foreach(_inc ${_mq_sdk_includes})
            if(EXISTS "${_inc}")
                list(APPEND _mq_valid_includes "${_inc}")
            endif()
        endforeach()

        target_include_directories(MQEngine::StandaloneSDK INTERFACE ${_mq_valid_includes})

        target_compile_definitions(MQEngine::StandaloneSDK INTERFACE
            ABSL_CONSUME_DLL
            BOOST_ATOMIC_DYN_LINK BOOST_ATOMIC_NO_LIB
            BOOST_CHRONO_DYN_LINK BOOST_CHRONO_NO_LIB
            BOOST_CONTAINER_DYN_LINK BOOST_CONTAINER_NO_LIB
            BOOST_DATE_TIME_DYN_LINK BOOST_DATE_TIME_NO_LIB
            BOOST_LOCALE_DYN_LINK BOOST_LOCALE_NO_LIB
            BOOST_SERIALIZATION_DYN_LINK BOOST_SERIALIZATION_NO_LIB
            BOOST_THREAD_DYN_LINK BOOST_THREAD_NO_LIB BOOST_THREAD_USE_DLL
            FMT_SHARED
            GLFW_DLL
            SPDLOG_COMPILED_LIB SPDLOG_FMT_EXTERNAL SPDLOG_SHARED_LIB
            TRACY_ENABLE TRACY_IMPORTS TRACY_ON_DEMAND
            FCT_PYBRI_DEFAULT_PYTHON_HOME="${MQ_VCPKG_INSTALLED_DIR}/tools/python3"
        )

        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            set(_vcpkg_lib_dir "${MQ_VCPKG_INSTALLED_DIR}/lib")
            set(_node_bin_dir "${MQENGINE_ROOT_DIR}/FCT/FCT_Node/node/Win/bin/Release")
            set(_spdlog_lib "${_vcpkg_lib_dir}/spdlog.lib")
            set(_fmt_lib "${_vcpkg_lib_dir}/fmt.lib")
        else()
            set(_vcpkg_lib_dir "${MQ_VCPKG_INSTALLED_DIR}/debug/lib")
            set(_node_bin_dir "${MQENGINE_ROOT_DIR}/FCT/FCT_Node/node/Win/bin/Debug")
            set(_spdlog_lib "${_vcpkg_lib_dir}/spdlogd.lib")
            set(_fmt_lib "${_vcpkg_lib_dir}/fmtd.lib")
        endif()

        target_link_directories(MQEngine::StandaloneSDK INTERFACE
            "${_node_bin_dir}"
            "${MQENGINE_ROOT_DIR}/FCT/FCT_Node/node/Win/bin/lib"
            "${_vcpkg_lib_dir}"
            "${MQ_VCPKG_INSTALLED_DIR}/lib"
        )

        # 收集 Boost 与 Abseil 动态导入库
        file(GLOB _boost_libs
            "${_vcpkg_lib_dir}/boost_locale*.lib"
            "${_vcpkg_lib_dir}/boost_serialization*.lib"
            "${_vcpkg_lib_dir}/boost_thread*.lib"
            "${_vcpkg_lib_dir}/boost_atomic*.lib"
            "${_vcpkg_lib_dir}/boost_chrono*.lib"
            "${_vcpkg_lib_dir}/boost_system*.lib"
            "${_vcpkg_lib_dir}/boost_date_time*.lib"
            "${_vcpkg_lib_dir}/boost_container*.lib"
        )
        file(GLOB _absl_libs "${_vcpkg_lib_dir}/absl_flags*.lib")

        if(EXISTS "${MQENGINE_BUILD_DIR}/Engine/Core/Engine.lib")
            set(_engine_import_lib "${MQENGINE_BUILD_DIR}/Engine/Core/Engine.lib")
        else()
            set(_engine_import_lib "${MQENGINE_BUILD_DIR}/Engine/Engine.lib")
        endif()

        set(_core_libs
            "${_engine_import_lib}"
            "${MQENGINE_BUILD_DIR}/FCT/FCT_IMGUI/FCT_IMGUI.lib"
            "${MQENGINE_BUILD_DIR}/FCT/FCT_RHI/FCT.lib"
            "${MQENGINE_BUILD_DIR}/FCT/FCT_Base/FCT_Base.lib"
            "${MQENGINE_BUILD_DIR}/FCT/FCT_Base/tracy/TracyClient.lib"
            "${MQENGINE_BUILD_DIR}/FCT/FCT_SAI/FCT_SAI.lib"
            "${MQENGINE_BUILD_DIR}/FCT/FCT_Node/FCT_Node.lib"
            "${MQENGINE_BUILD_DIR}/FCT/NBR/FCT_NBR.lib"
            "${MQENGINE_BUILD_DIR}/FCT/FCT_PyBri/FCT_PyBri.lib"
            "${MQENGINE_BUILD_DIR}/lib/work_contract.lib"
            "${_vcpkg_lib_dir}/abseil_dll.lib"
            "${_spdlog_lib}"
            "${_fmt_lib}"
            "${_vcpkg_lib_dir}/vulkan-1.lib"
            "${_vcpkg_lib_dir}/glfw3dll.lib"
            "${MQ_VCPKG_INSTALLED_DIR}/lib/python311.lib"
            ${_absl_libs}
            ${_boost_libs}
            libnode.lib
            d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib dwmapi.lib
            ws2_32.lib mswsock.lib shlwapi.lib ole32.lib windowscodecs.lib synchronization.lib
        )
        if(EXISTS "${MQENGINE_BUILD_DIR}/ThirdParty/ImGuiColorTextEdit/ImGuiColorTextEdit.lib")
            list(APPEND _core_libs "${MQENGINE_BUILD_DIR}/ThirdParty/ImGuiColorTextEdit/ImGuiColorTextEdit.lib")
        endif()

        target_link_libraries(MQEngine::StandaloneSDK INTERFACE ${_core_libs})
    endif()
endif()

include("${MQENGINE_ROOT_DIR}/Config/mq_reflect.cmake")

# ------------------------------------------------------------------------------
# 辅助函数：在 Standalone 或 In-Tree 模式下按插件名解析并导入上游依赖插件 Target
# ------------------------------------------------------------------------------
function(_mq_resolve_plugin_dependency dep_name)
    if(TARGET ${dep_name})
        return()
    endif()

    if(NOT MQ_PLUGIN_STANDALONE_BUILD)
        message(FATAL_ERROR "[MQPluginSDK] In-Tree 模式下未找到依赖插件目标 '${dep_name}'，请确保被依赖插件在 CMake 中先于当前插件添加")
    endif()

    set(_candidate_lib_paths
        "${MQENGINE_BUILD_DIR}/Engine/Plugins/${dep_name}/${dep_name}.lib"
        "${MQENGINE_BUILD_DIR}/UserCode/Plugins/${dep_name}/${dep_name}.lib"
        "${MQENGINE_BUILD_DIR}/Plugins/Engine/${dep_name}/${dep_name}.lib"
        "${MQENGINE_BUILD_DIR}/Plugins/User/${dep_name}/${dep_name}.lib"
        "${MQENGINE_BUILD_DIR}/Plugins/${dep_name}/${dep_name}.lib"
    )
    set(_dep_lib "")
    set(_dep_dll "")
    foreach(_cand ${_candidate_lib_paths})
        if(EXISTS "${_cand}")
            set(_dep_lib "${_cand}")
            get_filename_component(_cand_dir "${_cand}" DIRECTORY)
            set(_dep_dll "${_cand_dir}/${dep_name}.dll")
            break()
        endif()
    endforeach()

    if(NOT _dep_lib)
        message(FATAL_ERROR "[MQPluginSDK] Standalone 模式下未找到依赖插件 '${dep_name}' 的导入库 (.lib)，请先编译 ${dep_name}")
    endif()

    add_library(${dep_name} SHARED IMPORTED GLOBAL)
    set_target_properties(${dep_name} PROPERTIES
        IMPORTED_IMPLIB "${_dep_lib}"
        IMPORTED_LOCATION "${_dep_dll}"
    )

    set(_candidate_src_roots
        "${MQENGINE_ROOT_DIR}/Engine/Plugins/${dep_name}"
        "${MQENGINE_ROOT_DIR}/UserCode/Plugins/${dep_name}"
        "${MQENGINE_ROOT_DIR}/Plugins/Engine/${dep_name}"
        "${MQENGINE_ROOT_DIR}/Plugins/User/${dep_name}"
        "${MQENGINE_ROOT_DIR}/Plugins/${dep_name}"
    )
    foreach(_root ${_candidate_src_roots})
        if(EXISTS "${_root}/src")
            file(GLOB_RECURSE _dep_hdrs
                LIST_DIRECTORIES false
                "${_root}/src/*.h"
                "${_root}/src/*.hpp"
            )
            foreach(_dhdr ${_dep_hdrs})
                file(RELATIVE_PATH _drel "${_root}/src" "${_dhdr}")
                configure_file("${_dhdr}" "${_root}/include/${dep_name}/${_drel}" COPYONLY)
            endforeach()
            if(EXISTS "${_root}/src/${dep_name}.h")
                configure_file("${_root}/src/${dep_name}.h" "${_root}/include/${dep_name}.h" COPYONLY)
            endif()
        endif()
        if(EXISTS "${_root}/include")
            target_include_directories(${dep_name} INTERFACE "${_root}/include")
        endif()
        if(EXISTS "${_root}/src")
            target_include_directories(${dep_name} INTERFACE "${_root}/src")
        endif()
    endforeach()

    message(STATUS "[MQPluginSDK] 已自动导入预编译上游依赖插件: ${dep_name} (${_dep_lib})")
endfunction()

# ------------------------------------------------------------------------------
# 核心宏：mq_add_plugin
# 用法：
#   mq_add_plugin(<PluginName>
#       [BUILTIN]
#       [VERSION "1.0.0"]
#       [DEPENDS <UpstreamPlugin1> <UpstreamPlugin2> ...]
#       [SOURCES <src1> ...]
#       [HEADERS <hdr1> ...]
#       [REFLECT_HEADERS <rhdr1> ...]
#       [AUTO_REFLECT]
#       [USE_COMPUTE]
#       [LINK_LIBRARIES <lib1> ...]
#       [COMPILE_DEFINITIONS <def1> ...]
#   )
# ------------------------------------------------------------------------------
function(mq_add_plugin plugin_name)
    set(options BUILTIN AUTO_REFLECT USE_COMPUTE)
    set(oneValueArgs VERSION)
    set(multiValueArgs DEPENDS SOURCES HEADERS REFLECT_HEADERS LINK_LIBRARIES COMPILE_DEFINITIONS INCLUDE_DIRS)
    cmake_parse_arguments(MQ_PLUG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT MQ_PLUG_VERSION)
        set(MQ_PLUG_VERSION "1.0.0")
    endif()

    # 若未显式传入 SOURCES / HEADERS，统一调用 collect_module_files 递归收集 ./src 下全部子目录源码与头文件
    if(NOT MQ_PLUG_SOURCES OR NOT MQ_PLUG_HEADERS)
        collect_module_files(_PLUG "./src")
        if(NOT MQ_PLUG_SOURCES)
            set(MQ_PLUG_SOURCES ${_PLUG_MODULE_SOURCES})
        endif()
        if(NOT MQ_PLUG_HEADERS)
            set(MQ_PLUG_HEADERS ${_PLUG_MODULE_HEADERS})
        endif()
    endif()

    # 自动判定是否为引擎本体插件 (BUILTIN 或位于 Engine/Plugins、Plugins/Engine 目录下)
    set(_is_builtin 0)
    if(MQ_PLUG_BUILTIN OR
       "${CMAKE_CURRENT_SOURCE_DIR}" MATCHES "/Engine/Plugins/" OR
       "${CMAKE_CURRENT_SOURCE_DIR}" MATCHES "/Plugins/Engine/")
        set(_is_builtin 1)
    endif()

    if(ANDROID)
        set(_plug_lib_type STATIC)
    else()
        set(_plug_lib_type SHARED)
    endif()

    add_library(${plugin_name} ${_plug_lib_type}
        ${MQ_PLUG_SOURCES}
        ${MQ_PLUG_HEADERS}
    )

    string(TOUPPER "${plugin_name}" _upper_name)
    string(TOLOWER "${plugin_name}" _lower_name)
    string(REPLACE ";" "," _deps_csv "${MQ_PLUG_DEPENDS}")

    target_compile_definitions(${plugin_name} PRIVATE
        BUILD_${_upper_name}_PLUGIN
        MQ_PLUGIN_NAME="${plugin_name}"
        MQ_PLUGIN_VERSION="${MQ_PLUG_VERSION}"
        MQ_PLUGIN_DEPENDENCIES="${_deps_csv}"
        MQ_PLUGIN_IS_BUILTIN=${_is_builtin}
        ${MQ_PLUG_COMPILE_DEFINITIONS}
    )

    if(MSVC)
        target_compile_options(${plugin_name} PRIVATE /bigobj)
    endif()

    # 配置阶段立即同步一次头文件到 include/<PluginName>/，保证下游插件在配置期即可看见头文件
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/${plugin_name}")
    foreach(_hdr ${MQ_PLUG_HEADERS})
        if(NOT IS_ABSOLUTE "${_hdr}")
            set(_hdr "${CMAKE_CURRENT_SOURCE_DIR}/${_hdr}")
        endif()
        file(RELATIVE_PATH _rel_path "${CMAKE_CURRENT_SOURCE_DIR}/src" "${_hdr}")
        get_filename_component(_rel_dir "${_rel_path}" DIRECTORY)
        if(_rel_dir)
            file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/${plugin_name}/${_rel_dir}")
        endif()
        configure_file("${_hdr}" "${CMAKE_CURRENT_SOURCE_DIR}/include/${plugin_name}/${_rel_path}" COPYONLY)
    endforeach()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/${plugin_name}.h")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/src/${plugin_name}.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/${plugin_name}.h"
            COPYONLY
        )
    endif()

    add_copy_target_headers(${plugin_name} "${MQ_PLUG_HEADERS}")

    target_include_directories(${plugin_name} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
        ${MQ_PLUG_INCLUDE_DIRS}
    )

    # 链接引擎核心与 FCT 底座
    if(MQ_PLUGIN_STANDALONE_BUILD)
        target_link_libraries(${plugin_name} MQEngine::StandaloneSDK)
        if(MQ_PLUG_USE_COMPUTE)
            if(EXISTS "${MQENGINE_BUILD_DIR}/FCT/FCT_Compute/FCT_Compute.lib")
                target_link_libraries(${plugin_name}
                    "${MQENGINE_BUILD_DIR}/FCT/FCT_Compute/FCT_Compute.lib"
                    "${MQENGINE_BUILD_DIR}/bin/luisa-compute-core.lib"
                    "${MQENGINE_BUILD_DIR}/bin/luisa-compute-runtime.lib"
                    "${MQENGINE_BUILD_DIR}/bin/luisa-compute-dsl.lib"
                    "${MQENGINE_BUILD_DIR}/bin/luisa-compute-ast.lib"
                    "${MQENGINE_BUILD_DIR}/bin/luisa-compute-vstl.lib"
                )
            endif()
        endif()
    else()
        target_link_fct_libraries(${plugin_name})
        target_link_fct_sai_libraries(${plugin_name})
        target_link_fct_node_libraries(${plugin_name})
        target_link_fct_nbr_libraries(${plugin_name})
        target_link_fct_imgui_libraries(${plugin_name})
        if(MQ_PLUG_USE_COMPUTE AND COMMAND target_link_fct_compute_libraries)
            target_link_fct_compute_libraries(${plugin_name})
        endif()
        target_link_engine(${plugin_name})
    endif()

    set_utf8_for_target(${plugin_name})

    # 解析并链接 DEPENDS 声明的上游插件
    foreach(_dep ${MQ_PLUG_DEPENDS})
        _mq_resolve_plugin_dependency(${_dep})
        target_link_libraries(${plugin_name} ${_dep})
        if(NOT MQ_PLUGIN_STANDALONE_BUILD)
            add_dependencies(${plugin_name} ${_dep})
        endif()
    endforeach()

    if(MQ_PLUG_LINK_LIBRARIES)
        target_link_libraries(${plugin_name} ${MQ_PLUG_LINK_LIBRARIES})
    endif()

    # 反射代码生成支持
    if(MQ_PLUG_REFLECT_HEADERS)
        if(MQ_PLUG_AUTO_REFLECT)
            mq_target_reflect(
                TARGET ${plugin_name}
                AUTO_REFLECT
                HEADERS ${MQ_PLUG_REFLECT_HEADERS}
            )
        else()
            mq_target_reflect(
                TARGET ${plugin_name}
                HEADERS ${MQ_PLUG_REFLECT_HEADERS}
            )
        endif()
    endif()

    # 在 Standalone 模式下，编译完成后自动将 .dll 与 .lib 同步回主构建目录 Plugins/<PluginName>/ 与 Editor 运行目录
    if(MQ_PLUGIN_STANDALONE_BUILD AND WIN32)
        set(_dest_plugin_dir "${MQENGINE_BUILD_DIR}/Plugins/${plugin_name}")
        set(_dest_editor_dir "${MQENGINE_BUILD_DIR}/Applications/Editor")
        add_custom_command(TARGET ${plugin_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_dest_plugin_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${plugin_name}>" "${_dest_plugin_dir}/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_LINKER_FILE:${plugin_name}>" "${_dest_plugin_dir}/"
            COMMENT "[MQPluginSDK] Standalone 编译产物已同步至 ${_dest_plugin_dir}"
        )
    endif()

    message(STATUS "[MQPluginSDK] 已配置插件目标: ${plugin_name} (DEPENDS: ${MQ_PLUG_DEPENDS}, Standalone=${MQ_PLUGIN_STANDALONE_BUILD})")
endfunction()
