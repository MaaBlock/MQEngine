# Engine/Plugins/CliEditor/CMakeLists.txt
# CliEditor 渐进式命令行与 AI 编辑器插件包（编译出独立的 CliEditorComposite.dll）
include("${CMAKE_SOURCE_DIR}/Config/MQPluginSDK.cmake")

mq_add_composite(CliEditorComposite
    PLUGIN CliEditor
)

mq_add_plugin(MQCliEditor
    BUILTIN
    VERSION "1.0.0"
    COMPOSITES CliEditorComposite
)
