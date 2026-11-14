# Engine/Plugins/MQEditor/CMakeLists.txt
# MQEditor 是纯逻辑分组目录（不生成 MQEditor.dll），其下每个 Composite 独立编译为一个 .dll
include("${CMAKE_SOURCE_DIR}/Config/MQPluginSDK.cmake")

mq_add_composite(SceneViewerComposite
    PLUGIN MQEditor
    DEPENDS ImGuiComposite EditorUiComposite
)

mq_add_composite(AssetBrowserComposite
    PLUGIN MQEditor
    DEPENDS ImGuiComposite EditorUiComposite
)

mq_add_composite(ShaderEditorComposite
    PLUGIN MQEditor
    DEPENDS ImGuiComposite EditorUiComposite
)

mq_add_composite(RenderGraphEditorComposite
    PLUGIN MQEditor
    DEPENDS ImGuiComposite EditorUiComposite
)

find_package(zstd CONFIG REQUIRED)

set(TRACY_ROOT "${CMAKE_SOURCE_DIR}/FCT/FCT_Profiling/ThirdParty/tracy")
set(TRACY_SERVER_SOURCES
    ${TRACY_ROOT}/server/TracyBroadcast.cpp
    ${TRACY_ROOT}/server/TracyMemory.cpp
    ${TRACY_ROOT}/server/TracyLocks.cpp
    ${TRACY_ROOT}/server/TracyMmap.cpp
    ${TRACY_ROOT}/server/TracyPrint.cpp
    ${TRACY_ROOT}/server/TracySysUtil.cpp
    ${TRACY_ROOT}/server/TracyTaskDispatch.cpp
    ${TRACY_ROOT}/server/TracyTextureCompression.cpp
    ${TRACY_ROOT}/server/TracyThreadCompress.cpp
    ${TRACY_ROOT}/server/TracyWorker.cpp
    ${TRACY_ROOT}/public/common/tracy_lz4.cpp
    ${TRACY_ROOT}/public/common/tracy_lz4hc.cpp
    ${TRACY_ROOT}/public/common/TracySocket.cpp
    ${TRACY_ROOT}/public/common/TracyStackFrames.cpp
    ${TRACY_ROOT}/public/common/TracySystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Composites/TracyProfilerComposite/shims/TracyViewShims.cpp
    ${TRACY_ROOT}/profiler/src/ini.c
    ${TRACY_ROOT}/profiler/src/Filters.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyBadVersion.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyColor.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyConfig.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyEmbed.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyEventDebug.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyFileselector.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyFilesystem.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyImGui.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyMicroArchitecture.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyMouse.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyNameGen.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyProtoHistory.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracySourceContents.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracySourceTokenizer.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracySourceView.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyStorage.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyTimelineController.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyTimelineItem.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyTimelineItemCpuData.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyTimelineItemGpu.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyTimelineItemPlot.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyTimelineItemThread.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyUtility.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_Annotations.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_Callstack.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_Compare.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_ConnectionState.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_ContextSwitch.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_CpuData.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_FindZone.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_FlameGraph.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_FrameOverview.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_FrameStatistics.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_FrameTimeline.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_FrameTree.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_GpuTimeline.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_Locks.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_Memory.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_Messages.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_Navigation.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_NotificationArea.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_Options.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_Playback.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_Plots.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_Ranges.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_Samples.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_Statistics.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_Timeline.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_TraceInfo.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_Utility.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_ZoneInfo.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyView_ZoneTimeline.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyWeb.cpp
    ${TRACY_ROOT}/profiler/src/profiler/TracyWindowConstraints.cpp
)

set(TRACY_IMGUI_SHIM_DIR "${CMAKE_CURRENT_BINARY_DIR}/tracy_imgui_shim")
set(TRACY_IMGUI_SHIM_HEADER "${TRACY_IMGUI_SHIM_DIR}/imgui.h")
set(TRACY_IMGUI_INTERNAL_SHIM_HEADER "${TRACY_IMGUI_SHIM_DIR}/imgui_internal.h")
set(TRACY_IMGUI_SHIM_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/src/Composites/TracyProfilerComposite/shims/GenerateTracyImGuiShim.cmake")

add_custom_command(
    OUTPUT "${TRACY_IMGUI_SHIM_HEADER}" "${TRACY_IMGUI_INTERNAL_SHIM_HEADER}"
    COMMAND ${CMAKE_COMMAND}
        "-DIMGUI_HEADER=${CMAKE_SOURCE_DIR}/FCT/FCT_IMGUI/imgui/imgui.h"
        "-DIMGUI_INTERNAL_HEADER=${CMAKE_SOURCE_DIR}/FCT/FCT_IMGUI/imgui/imgui_internal.h"
        "-DOUTPUT_HEADER=${TRACY_IMGUI_SHIM_HEADER}"
        "-DOUTPUT_INTERNAL_HEADER=${TRACY_IMGUI_INTERNAL_SHIM_HEADER}"
        -P "${TRACY_IMGUI_SHIM_SCRIPT}"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/FCT/FCT_IMGUI/imgui/imgui.h"
        "${CMAKE_SOURCE_DIR}/FCT/FCT_IMGUI/imgui/imgui_internal.h"
        "${TRACY_IMGUI_SHIM_SCRIPT}"
    COMMENT "Generating Tracy ImGui shim headers"
)
add_custom_target(MQTracyImGuiShim
    DEPENDS "${TRACY_IMGUI_SHIM_HEADER}" "${TRACY_IMGUI_INTERNAL_SHIM_HEADER}"
)

add_library(MQTracyServer STATIC ${TRACY_SERVER_SOURCES})
add_dependencies(MQTracyServer MQTracyImGuiShim)
set_utf8_for_target(MQTracyServer)
target_include_directories(MQTracyServer BEFORE PRIVATE
    "${TRACY_IMGUI_SHIM_DIR}"
)
target_include_directories(MQTracyServer PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Composites/TracyProfilerComposite/shims"
    "${TRACY_ROOT}/profiler/src/profiler"
    "${TRACY_ROOT}/profiler/src"
    "${TRACY_ROOT}/dtl"
    "${CMAKE_SOURCE_DIR}/FCT/FCT_IMGUI/imgui"
    "${TRACY_ROOT}/server"
    "${TRACY_ROOT}/public/common"
    "${TRACY_ROOT}/public"
)
target_compile_options(MQTracyServer PRIVATE "/UTRACY_ENABLE" "/D_CRT_SECURE_NO_WARNINGS")
target_compile_definitions(MQTracyServer PUBLIC
    TRACY_NO_ROOT_WINDOW
    TRACY_NO_FILESELECTOR
    IMGUI_DEFINE_MATH_OPERATORS
    NOMINMAX
)
target_link_libraries(MQTracyServer PUBLIC zstd::libzstd ws2_32 dbghelp)

mq_add_composite(TracyProfilerComposite
    PLUGIN MQEditor
    DEPENDS ImGuiComposite EditorUiComposite
    LIBS MQTracyServer zstd::libzstd ws2_32 dbghelp
    INCLUDES
        "${TRACY_IMGUI_SHIM_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Composites/TracyProfilerComposite/shims"
        "${TRACY_ROOT}/profiler/src/profiler"
        "${TRACY_ROOT}/profiler/src"
        "${TRACY_ROOT}/dtl"
        "${CMAKE_SOURCE_DIR}/FCT/FCT_IMGUI/imgui"
        "${TRACY_ROOT}/server"
        "${TRACY_ROOT}/public/common"
        "${TRACY_ROOT}/public"
)
add_dependencies(TracyProfilerComposite MQTracyImGuiShim)
target_include_directories(TracyProfilerComposite BEFORE PRIVATE
    "${TRACY_IMGUI_SHIM_DIR}"
)
target_compile_options(TracyProfilerComposite PRIVATE "/UTRACY_ENABLE")
target_compile_definitions(TracyProfilerComposite PRIVATE
    TRACY_ENABLE=0
    FCT_PROFILING_FORCE_DISABLE=1
    TRACY_NO_ROOT_WINDOW
    TRACY_NO_FILESELECTOR
    IMGUI_DEFINE_MATH_OPERATORS
    NOMINMAX
)

set(VCPKG_ZSTD_DLL "$<IF:$<CONFIG:Debug>,${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/debug/bin/zstd.dll,${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/zstd.dll>")

add_custom_command(TARGET TracyProfilerComposite POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_ZSTD_DLL}"
        "${CMAKE_BINARY_DIR}/Applications/Editor/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_ZSTD_DLL}"
        "${CMAKE_BINARY_DIR}/Applications/Editor/Plugins/MQEditor/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_ZSTD_DLL}"
        "${CMAKE_BINARY_DIR}/Plugins/MQEditor/"
    COMMENT "Staging zstd.dll for TracyProfilerComposite"
)

mq_add_composite(ReplayTimelineComposite
    PLUGIN MQEditor
    DEPENDS ImGuiComposite EditorUiComposite
)

mq_add_composite(NtcBakerComposite
    PLUGIN MQEditor
    DEPENDS ImGuiComposite EditorUiComposite
)

mq_add_composite(ProjectPackaging
    PLUGIN MQEditor
    DEPENDS ImGuiComposite EditorUiComposite
)

mq_add_plugin(MQEditor
    BUILTIN
    VERSION "1.0.0"
    COMPOSITES SceneViewerComposite AssetBrowserComposite ShaderEditorComposite RenderGraphEditorComposite TracyProfilerComposite ReplayTimelineComposite NtcBakerComposite ProjectPackaging
)

