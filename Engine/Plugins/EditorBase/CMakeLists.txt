# Engine/Plugins/EditorBase/CMakeLists.txt
# EditorBase 是纯逻辑分组目录（不生成 EditorBase.dll），其下每个 Composite 独立编译为一个 .dll
include("${CMAKE_SOURCE_DIR}/Config/MQPluginSDK.cmake")

mq_add_composite(ImGuiComposite
    PLUGIN EditorBase
)

mq_add_composite(EditorUiComposite
    PLUGIN EditorBase
    DEPENDS ImGuiComposite
    LIBS $<$<PLATFORM_ID:Windows>:advapi32 ole32 shell32 user32>
)

mq_add_plugin(EditorBase
    BUILTIN
    VERSION "1.0.0"
    COMPOSITES ImGuiComposite EditorUiComposite
)
