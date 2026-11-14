# Engine/Plugins/Blueprint/CMakeLists.txt
# Blueprint 插件包（纯目录分组，内含独立的 BlueprintRuntimeComposite.dll 与 BlueprintEditorComposite.dll）
include("${CMAKE_SOURCE_DIR}/Config/MQPluginSDK.cmake")

mq_add_composite(BlueprintRuntimeComposite
    PLUGIN Blueprint
)
target_include_directories(BlueprintRuntimeComposite PRIVATE
    "${CMAKE_SOURCE_DIR}/Engine/Plugins/GameHtmlUI/src"
    "${CMAKE_SOURCE_DIR}/Engine/Plugins/GameHtmlUI/ThirdParty/RmlUi/Include"
)
target_link_libraries(BlueprintRuntimeComposite RmlUiComposite)
if(MSVC)
    target_link_libraries(BlueprintRuntimeComposite delayimp)
    target_link_options(BlueprintRuntimeComposite PRIVATE "/DELAYLOAD:RmlUiComposite.dll")
endif()

mq_add_composite(BlueprintEditorComposite
    PLUGIN Blueprint
    DEPENDS BlueprintRuntimeComposite ImGuiComposite EditorUiComposite
)

mq_add_plugin(Blueprint
    BUILTIN
    VERSION "1.0.0"
    COMPOSITES BlueprintRuntimeComposite BlueprintEditorComposite
)
