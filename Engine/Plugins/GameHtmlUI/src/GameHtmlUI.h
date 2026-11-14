/**
 * @file GameHtmlUI.h
 * @brief GameHtmlUI 插件聚合主头文件（小游戏与纯运行时构建自动剥离编辑器头文件依赖）
 */
#ifndef MQENGINE_GAME_HTML_UI_H
#define MQENGINE_GAME_HTML_UI_H

#include "GameHtmlUiApi.h"
#include "Composites/RmlUiComposite/MQRmlFontEngine.h"
#include "Composites/RmlUiComposite/RmlUiBackendInterfaces.h"
#include "Composites/RmlUiComposite/RmlUiMiniGameAdapters.h"
#include "Composites/RmlUiComposite/RmlUiTouchInput.h"
#include "Composites/RmlUiComposite/RmlUiComposite.h"

#if !defined(MQ_HTMLUI_RUNTIME_ONLY) && !defined(MQ_MINIGAME_BUILD) && !defined(__EMSCRIPTEN__)
#include "Composites/HtmlUiEditorComposite/HtmlUiEditorComposite.h"
#endif

#endif // MQENGINE_GAME_HTML_UI_H
