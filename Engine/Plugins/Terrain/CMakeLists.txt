# Engine/Plugins/Terrain/CMakeLists.txt
# Terrain 插件包（纯目录分组，内含独立的 TerrainRuntimeComposite.dll 与 TerrainEditorComposite.dll）
include("${CMAKE_SOURCE_DIR}/Config/MQPluginSDK.cmake")

mq_add_composite(TerrainRuntimeComposite
    PLUGIN Terrain
)

mq_add_composite(TerrainEditorComposite
    PLUGIN Terrain
    DEPENDS TerrainRuntimeComposite ImGuiComposite EditorUiComposite
)

mq_add_plugin(Terrain
    BUILTIN
    VERSION "1.0.0"
    COMPOSITES TerrainRuntimeComposite TerrainEditorComposite
)
