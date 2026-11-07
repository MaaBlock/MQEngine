#ifndef MQENGINE_IHTML_GAME_UI_SERVICE_H
#define MQENGINE_IHTML_GAME_UI_SERVICE_H

#include <string>
#include <functional>
#include "thirdparty/thirdparty.h"

namespace MQEngine {

/**
 * @brief HTML/RML 游戏 UI 服务接口，面向游戏运行时提供 HTML/RML + RCSS 页面加载、DOM 操作与事件交互
 */
class IHtmlGameUiService {
public:
    using EventCallback = std::function<void(const std::string& param)>;
    using ActionCallback = std::function<void()>;

    virtual ~IHtmlGameUiService() = default;

    /**
     * @brief 加载指定 RML / HTML 页面文档
     * @param rmlPath 页面文件路径（如 "Assets/UI/launcher.rml"）
     */
    virtual Status loadDocument(const std::string& rmlPath) = 0;

    /**
     * @brief 关闭或卸载当前页面文档
     */
    virtual void closeDocument() = 0;

    /**
     * @brief 注册元素交互事件监听（如 click、change 等）
     */
    virtual void addEventListener(const std::string& elementId,
                                  const std::string& eventType,
                                  EventCallback callback) = 0;

    /**
     * @brief 注册命名动作（Action），可由 HTML 中触发
     */
    virtual void registerAction(const std::string& actionName,
                                EventCallback callback) = 0;

    /**
     * @brief 设置指定元素的 Inner RML / HTML 内容
     */
    virtual void setElementInnerRml(const std::string& elementId,
                                    const std::string& rml) = 0;

    /**
     * @brief 设置指定元素的纯文本内容
     */
    virtual void setElementText(const std::string& elementId,
                                const std::string& text) = 0;

    /**
     * @brief 获取表单控件（input / select）的值
     */
    virtual std::string getElementValue(const std::string& elementId) const = 0;

    /**
     * @brief 设置表单控件的值
     */
    virtual void setElementValue(const std::string& elementId,
                                 const std::string& value) = 0;

    /**
     * @brief 获取复选框的勾选状态
     */
    virtual bool getElementChecked(const std::string& elementId) const = 0;

    /**
     * @brief 设置复选框的勾选状态
     */
    virtual void setElementChecked(const std::string& elementId,
                                   bool checked) = 0;

    /**
     * @brief 设置元素的 CSS 属性（如 display: none / block）
     */
    virtual void setElementProperty(const std::string& elementId,
                                    const std::string& propertyName,
                                    const std::string& propertyValue) = 0;

    /**
     * @brief 添加或移除元素的 CSS 类名
     */
    virtual void setElementClass(const std::string& elementId,
                                 const std::string& className,
                                 bool enable) = 0;

    /**
     * @brief 刷新数据绑定或 DOM 状态
     */
    virtual void update() {}
};

} // namespace MQEngine

#endif // MQENGINE_IHTML_GAME_UI_SERVICE_H
