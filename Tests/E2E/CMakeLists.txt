set(E2E_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Common")
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" E2E_REPO_ROOT_POSIX)

set(E2E_COMMON_SOURCES
    ${E2E_COMMON_DIR}/E2ETestRegistry.cpp
    ${E2E_COMMON_DIR}/HeadlessGlesFixture.cpp
)

set(E2E_COMMON_HEADERS
    ${E2E_COMMON_DIR}/E2ECommon.h
    ${E2E_COMMON_DIR}/E2EAssert.h
    ${E2E_COMMON_DIR}/E2ETestBase.h
    ${E2E_COMMON_DIR}/E2ETestRegistry.h
    ${E2E_COMMON_DIR}/TestProfiles.h
    ${E2E_COMMON_DIR}/HeadlessGlesFixture.h
)

function(add_e2e_runner target_name main_src tier_label timeout_sec)
    add_executable(${target_name}
        ${main_src}
        ${E2E_COMMON_SOURCES}
        ${E2E_COMMON_HEADERS}
    )

    target_include_directories(${target_name} PRIVATE
        ${E2E_COMMON_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        "${CMAKE_BINARY_DIR}/generated-include"
        "${CMAKE_SOURCE_DIR}/FCT/FCT_RHI/include"
        "${CMAKE_SOURCE_DIR}/FCT/FCT_RHI/include/FCT"
        "${CMAKE_SOURCE_DIR}/FCT/FCT_Base/include"
        "${CMAKE_SOURCE_DIR}/FCT/FCT_Profiling/include"
        "${CMAKE_SOURCE_DIR}/FCT/FCT_SAI/include"
        "${CMAKE_SOURCE_DIR}/FCT/FCT_Compute/include"
        "${CMAKE_SOURCE_DIR}/FCT/FCT_WebJS/include"
        "${CMAKE_SOURCE_DIR}/Engine/Plugins/DouyinMiniGame/include"
        "${CMAKE_SOURCE_DIR}/Engine/Plugins/GameHtmlUI/include"
        "${CMAKE_SOURCE_DIR}/Engine/Plugins/GameHtmlUI/ThirdParty/RmlUi/Include"
    )

    target_compile_definitions(${target_name} PRIVATE
        WIN32_LEAN_AND_MEAN
        MQ_E2E_REPO_ROOT="${E2E_REPO_ROOT_POSIX}"
        MQ_HTMLUI_RUNTIME_ONLY=1
        RMLUI_STATIC_LIB
    )

    # Note: 统一使用无关键字 target_link_libraries，与 target_link_fct_libraries / target_link_engine 签名严格一致
    if(TARGET FCT_WebJS)
        target_link_libraries(${target_name} FCT_WebJS)
    endif()
    target_link_fct_libraries(${target_name})
    target_link_fct_sai_libraries(${target_name})
    if(COMMAND target_link_fct_compute_libraries)
        target_link_fct_compute_libraries(${target_name})
    elseif(TARGET FCT_Compute)
        target_link_libraries(${target_name} FCT_Compute)
    endif()
    target_link_engine(${target_name})

    foreach(_e2e_plugin_lib
        FontRuntimeComposite
        MQRmlUiCore
        RmlUiComposite
        DouyinPackagerCliComposite
        DouyinPackagerEditorComposite
        DouyinPlatformRuntimeComposite
    )
        if(TARGET ${_e2e_plugin_lib})
            target_link_libraries(${target_name} ${_e2e_plugin_lib})
        endif()
    endforeach()

    # Note: 剥离桌面 FCT_Node 静态库依赖，确保 FCT::JS::* 符号统一解析至 FCT_WebJS
    get_target_property(_e2e_libs ${target_name} LINK_LIBRARIES)
    if(_e2e_libs)
        list(REMOVE_ITEM _e2e_libs FCT_Node)
        set_target_properties(${target_name} PROPERTIES LINK_LIBRARIES "${_e2e_libs}")
    endif()

    set_utf8_for_target(${target_name})

    if(WIN32)
        foreach(_e2e_runtime_dll
            FontRuntimeComposite
            RmlUiComposite
            DouyinPackagerCliComposite
            DouyinPackagerEditorComposite
            DouyinPlatformRuntimeComposite
            ImGuiComposite
            EditorUiComposite
        )
            if(TARGET ${_e2e_runtime_dll})
                add_dependencies(${target_name} ${_e2e_runtime_dll})
                add_custom_command(TARGET ${target_name} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:${_e2e_runtime_dll}>"
                    "$<TARGET_FILE_DIR:${target_name}>/"
                    COMMENT "Staging ${_e2e_runtime_dll}.dll for ${target_name}"
                )
            endif()
        endforeach()

        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/$<IF:$<CONFIG:Debug>,debug/,>bin/abseil_dll.dll"
            "$<TARGET_FILE_DIR:${target_name}>/abseil_dll.dll"
            COMMENT "Ensuring correct abseil_dll.dll is copied for ${target_name}"
        )
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /bigobj /EHsc)
    else()
        target_compile_options(${target_name} PRIVATE -fno-exceptions)
    endif()

    add_test(NAME ${target_name} COMMAND ${target_name} --headless)
    set_tests_properties(${target_name} PROPERTIES
        TIMEOUT ${timeout_sec}
        LABELS "E2E;${tier_label}"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/Applications/CliEditor;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/debug/bin;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
    )
endfunction()

# ==============================================================================
# Tier 1 功能用例集 (FEAT-01 ~ FEAT-73 全量 365 测试)
# ==============================================================================
set(E2E_TIER1_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier1_Functional/Test_Tier1_FCT_RHI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier1_Functional/Test_Tier1_FCT_Compute.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier1_Functional/Test_Tier1_Engine_Renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier1_Functional/Test_Tier1_Engine_Core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier1_Functional/Test_Tier1_Plugins.cpp
)

add_e2e_runner(Test_E2E_Tier1 "Main_E2E_Tier1.cpp" "Tier1;Functional" 60)
target_sources(Test_E2E_Tier1 PRIVATE ${E2E_TIER1_SOURCES})

# ==============================================================================
# Tier 2 边界极限与降级容错用例集 (FEAT-01 ~ FEAT-73 全量 365 测试)
# ==============================================================================
set(E2E_TIER2_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier2_Boundary/Test_Tier2_FCT_RHI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier2_Boundary/Test_Tier2_FCT_Compute.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier2_Boundary/Test_Tier2_Engine_Renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier2_Boundary/Test_Tier2_Engine_Core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier2_Boundary/Test_Tier2_Plugins.cpp
)

add_e2e_runner(Test_E2E_Tier2 "Main_E2E_Tier2.cpp" "Tier2;Boundary" 60)
target_sources(Test_E2E_Tier2 PRIVATE ${E2E_TIER2_SOURCES})

# ==============================================================================
# Tier 3 正交两两组合测试集 (FEAT-01 ~ FEAT-73 100% 覆盖, 84 测试)
# ==============================================================================
set(E2E_TIER3_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier3_Pairwise/Test_Tier3_RHI_RenderGraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier3_Pairwise/Test_Tier3_Renderer_Clustering_GPUScene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier3_Pairwise/Test_Tier3_Asset_WorkHost_SAI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier3_Pairwise/Test_Tier3_Font_HID_UI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier3_Pairwise/Test_Tier3_Douyin_Packaging_Runtime.cpp
)

add_e2e_runner(Test_E2E_Tier3 "Main_E2E_Tier3.cpp" "Tier3;Pairwise" 120)
target_sources(Test_E2E_Tier3 PRIVATE ${E2E_TIER3_SOURCES})

# ==============================================================================
# Tier 4 真实负载端到端综合场景测试集 (S1 ~ S5 全链路闭环, 42 场景)
# ==============================================================================
set(E2E_TIER4_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier4_Scenario/Test_Tier4_S1_CombatSwarmWorkload.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier4_Scenario/Test_Tier4_S2_ZeroKbFontUiRoaming.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier4_Scenario/Test_Tier4_S3_CrossPlatformInputEquivalence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier4_Scenario/Test_Tier4_S4_StreamingCacheWeakNetwork.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tier4_Scenario/Test_Tier4_S5_DouyinEcosystemAndCookLoop.cpp
)

add_e2e_runner(Test_E2E_Tier4 "Main_E2E_Tier4.cpp" "Tier4;Scenarios" 240)
target_sources(Test_E2E_Tier4 PRIVATE ${E2E_TIER4_SOURCES})

# ==============================================================================
# Tier All 全量合并回归套件 (857 测试)
# ==============================================================================
add_e2e_runner(Test_E2E_All "Main_E2E_All.cpp" "TierAll;Consolidated" 360)
target_sources(Test_E2E_All PRIVATE
    ${E2E_TIER1_SOURCES}
    ${E2E_TIER2_SOURCES}
    ${E2E_TIER3_SOURCES}
    ${E2E_TIER4_SOURCES}
)
