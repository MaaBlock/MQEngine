#ifndef EDITOR_IUI_MODULE_H
#define EDITOR_IUI_MODULE_H

#include "UiContext.h"

namespace MQEngine {

    /**
     * @brief UI模块抽象基类，所有UI模块仅关注ImGui视图构建与强类型信号发射
     */
    class IUiModule {
    public:
        virtual ~IUiModule() = default;

        virtual std::string_view getModuleName() const = 0;

        virtual void onInit(UiContext& ctx) {
            (void)ctx;
        }

        virtual void onRender(UiContext& ctx) = 0;

        virtual void onTerm(UiContext& ctx) {
            (void)ctx;
        }

        bool isEnabled() const { return m_enabled; }
        void setEnabled(bool enabled) { m_enabled = enabled; }

    private:
        bool m_enabled = true;
    };

    /**
     * @brief 遗留面板模块适配器，将具备 render() 接口的既有面板无缝接入 UI 层管线
     */
    template <typename PanelType>
    class UiLegacyPanelModule : public IUiModule {
    public:
        explicit UiLegacyPanelModule(std::string name, PanelType* panel, bool takeOwnership = true)
            : m_name(std::move(name))
            , m_panel(panel)
            , m_ownsPanel(takeOwnership) {}

        ~UiLegacyPanelModule() override {
            releasePanel();
        }

        std::string_view getModuleName() const override {
            return m_name;
        }

        void onRender(UiContext& ctx) override {
            (void)ctx;
            if (m_panel) {
                m_panel->render();
            }
        }

        void onTerm(UiContext& ctx) override {
            (void)ctx;
            releasePanel();
        }

        PanelType* getPanel() const { return m_panel; }

    private:
        void releasePanel() {
            if (m_ownsPanel && m_panel) {
                delete m_panel;
                m_panel = nullptr;
            }
        }

        std::string m_name;
        PanelType* m_panel = nullptr;
        bool m_ownsPanel = true;
    };

} // namespace MQEngine

#endif // EDITOR_IUI_MODULE_H
