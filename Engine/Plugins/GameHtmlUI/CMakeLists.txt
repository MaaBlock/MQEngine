# Engine/Plugins/GameHtmlUI/CMakeLists.txt
# GameHtmlUI 是纯逻辑分组目录（不生成 GameHtmlUI.dll），其下 RmlUiComposite 与 HtmlUiEditorComposite 各自独立编译为 .dll
include("${CMAKE_SOURCE_DIR}/Config/MQPluginSDK.cmake")

option(MQ_HTMLUI_ENABLE_DEBUGGER "启用 RmlUi::Debugger 运行时调试器" OFF)
option(MQ_HTMLUI_RUNTIME_ONLY "仅构建轻量级 RmlUiComposite 运行时（剥离 HtmlUiEditorComposite 与调试层）" OFF)

if(EMSCRIPTEN OR MQ_MINIGAME_BUILD)
    set(MQ_HTMLUI_ENABLE_DEBUGGER OFF CACHE BOOL "小游戏目标强制裁剪 RmlUi::Debugger" FORCE)
    set(MQ_HTMLUI_RUNTIME_ONLY ON CACHE BOOL "小游戏目标强制仅保留轻量运行时" FORCE)
endif()

set(RMLUI_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/RmlUi")

find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

file(GLOB_RECURSE RMLUI_CORE_SOURCES
    "${RMLUI_THIRD_PARTY_DIR}/Source/Core/*.cpp"
)

# FEAT-47: 物理剔除 RmlUi 内置 FontEngineDefault（彻底斩断内置 FreeType/libpng/brotli/zlib 冗余静态链接）
list(FILTER RMLUI_CORE_SOURCES EXCLUDE REGEX "FontEngineDefault/.*\\.cpp$")

set(MQRMLUI_SOURCES ${RMLUI_CORE_SOURCES})
if(MQ_HTMLUI_ENABLE_DEBUGGER)
    file(GLOB_RECURSE RMLUI_DEBUGGER_SOURCES
        "${RMLUI_THIRD_PARTY_DIR}/Source/Debugger/*.cpp"
    )
    list(APPEND MQRMLUI_SOURCES ${RMLUI_DEBUGGER_SOURCES})
endif()

add_library(MQRmlUiCore STATIC
    ${MQRMLUI_SOURCES}
)

target_include_directories(MQRmlUiCore PUBLIC
    "${RMLUI_THIRD_PARTY_DIR}/Include"
)

target_compile_definitions(MQRmlUiCore PUBLIC
    RMLUI_STATIC_LIB
    NOMINMAX
)
set_utf8_for_target(MQRmlUiCore)

mq_add_composite(RmlUiComposite
    PLUGIN GameHtmlUI
    DEPENDS FontRuntimeComposite
    LIBS MQRmlUiCore spdlog::spdlog fmt::fmt
)
target_link_fct_sai_libraries(RmlUiComposite)
if(MSVC)
    target_link_libraries(RmlUiComposite delayimp)
    target_link_options(RmlUiComposite PRIVATE "/DELAYLOAD:FontRuntimeComposite.dll")
endif()
target_compile_definitions(RmlUiComposite PUBLIC
    RMLUI_STATIC_LIB
    NOMINMAX
    $<$<BOOL:${MQ_HTMLUI_ENABLE_DEBUGGER}>:MQ_HTMLUI_DEBUGGER_ENABLED=1>
    $<$<NOT:$<BOOL:${MQ_HTMLUI_ENABLE_DEBUGGER}>>:MQ_HTMLUI_DEBUGGER_ENABLED=0>
    $<$<BOOL:${MQ_HTMLUI_RUNTIME_ONLY}>:MQ_HTMLUI_RUNTIME_ONLY=1>
)

set(GAME_HTML_UI_ACTIVE_COMPOSITES RmlUiComposite)

if(NOT MQ_HTMLUI_RUNTIME_ONLY)
    mq_add_composite(HtmlUiEditorComposite
        PLUGIN GameHtmlUI
        DEPENDS ImGuiComposite EditorUiComposite BlueprintEditorComposite
        LIBS spdlog::spdlog fmt::fmt BlueprintEditorComposite
    )
    target_compile_definitions(HtmlUiEditorComposite PRIVATE RMLUI_STATIC_LIB)
    list(APPEND GAME_HTML_UI_ACTIVE_COMPOSITES HtmlUiEditorComposite)
endif()

mq_add_plugin(GameHtmlUI
    BUILTIN
    VERSION "1.0.0"
    COMPOSITES ${GAME_HTML_UI_ACTIVE_COMPOSITES}
)
