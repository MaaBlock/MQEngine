# Engine/Plugins/DouyinMiniGame/CMakeLists.txt
# 抖音小游戏一键烘焙打包与宿主生态运行时官方插件包（三 Composite 物理隔离）
include("${CMAKE_SOURCE_DIR}/Config/MQPluginSDK.cmake")

# 1. 无头离线烘焙与五步发布流水线 DLL（Cli, Cook）
mq_add_composite(DouyinPackagerCliComposite
    PLUGIN DouyinMiniGame
    DEPENDS FontRuntimeComposite
)
if(MSVC)
    target_link_libraries(DouyinPackagerCliComposite delayimp)
    target_link_options(DouyinPackagerCliComposite PRIVATE "/DELAYLOAD:FontRuntimeComposite.dll")
endif()

# 2. 桌面编辑器可视化配置与一键烘焙预览面板 DLL（Editor, ImGui）
mq_add_composite(DouyinPackagerEditorComposite
    PLUGIN DouyinMiniGame
    DEPENDS ImGuiComposite EditorUiComposite DouyinPackagerCliComposite
)

# 3. 抖音小游戏运行时宿主生态服务 DLL（Runtime, MiniGame, Douyin）
# Note: 绑定 FCT_WebJS 句柄表并剥离桌面 FCT_Node 静态库依赖，杜绝 FCT::JS::Object 符号冲突与桌面 Node 泄漏
mq_add_composite(DouyinPlatformRuntimeComposite
    PLUGIN DouyinMiniGame
    DEPENDS FontRuntimeComposite
    LIBS FCT_WebJS RmlUiComposite
)
target_include_directories(DouyinPlatformRuntimeComposite PRIVATE
    "${CMAKE_SOURCE_DIR}/Engine/Plugins/GameHtmlUI/src"
    "${CMAKE_SOURCE_DIR}/Engine/Plugins/GameHtmlUI/ThirdParty/RmlUi/Include"
)
if(MSVC)
    target_link_libraries(DouyinPlatformRuntimeComposite delayimp)
    target_link_options(DouyinPlatformRuntimeComposite PRIVATE
        "/DELAYLOAD:RmlUiComposite.dll"
        "/DELAYLOAD:FontRuntimeComposite.dll"
    )
endif()

get_target_property(_douyin_rt_libs DouyinPlatformRuntimeComposite LINK_LIBRARIES)
if(_douyin_rt_libs)
    list(REMOVE_ITEM _douyin_rt_libs FCT_Node)
    set_target_properties(DouyinPlatformRuntimeComposite PROPERTIES LINK_LIBRARIES "${_douyin_rt_libs}")
endif()

mq_add_plugin(DouyinMiniGame
    BUILTIN
    VERSION "1.0.0"
    COMPOSITES
        DouyinPackagerEditorComposite
        DouyinPackagerCliComposite
        DouyinPlatformRuntimeComposite
)
