# Engine/Plugins/Font/CMakeLists.txt
# Font 零拷贝多端统一字体子系统插件包（输出独立 FontRuntimeComposite.dll）
include("${CMAKE_SOURCE_DIR}/Config/MQPluginSDK.cmake")

mq_add_composite(FontRuntimeComposite
    PLUGIN Font
)
target_link_fct_sai_libraries(FontRuntimeComposite)

mq_add_plugin(Font
    BUILTIN
    VERSION "1.0.0"
    COMPOSITES FontRuntimeComposite
)
