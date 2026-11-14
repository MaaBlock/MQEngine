/**
 * @file FontRuntimeComposite.h
 * @brief Font 插件运行时 Composite：统一管理 FontSystem 生命周期与跨插件无锁访问入口
 */
#ifndef MQENGINE_FONT_RUNTIME_COMPOSITE_H
#define MQENGINE_FONT_RUNTIME_COMPOSITE_H

#include "../../FontApi.h"
#include "FontSystem.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/interface/Composite.h>

namespace MQEngine
{
    namespace Font
    {
        /**
         * @brief 全端统一字体运行时 Composite，集中托管 FontSystem 实例并提供无锁全局解析入口
         */
        class FONT_RUNTIME_COMPOSITE_API FontRuntimeComposite : public Asset::Composite
        {
        public:
            FontRuntimeComposite();
            ~FontRuntimeComposite() override;

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "FontRuntimeComposite"; }
            std::string getPluginName() const override { return "Font"; }
            std::vector<std::string> getDependencies() const override { return {}; }
            std::vector<std::string> getRequiredTags() const override { return {"Runtime"}; }

            /**
             * @brief 获取当前 Composite 托管的 FontSystem 实例
             */
            [[nodiscard]] Resource::Font::FontSystem* getFontSystem() noexcept;
            [[nodiscard]] const Resource::Font::FontSystem* getFontSystem() const noexcept;

            /**
             * @brief 无锁获取当前已装载的 FontRuntimeComposite 活动实例
             */
            [[nodiscard]] static FontRuntimeComposite* InstancePtr() noexcept;

            /**
             * @brief 无锁获取全局活动 FontSystem（优先返回当前活动实例，必要时回查 CompositeManager）
             */
            [[nodiscard]] static Resource::Font::FontSystem* getActiveFontSystem() noexcept;

        private:
            UniquePtr<Resource::Font::FontSystem> m_fontSystem;
        };
    } // namespace Font
} // namespace MQEngine

#endif // MQENGINE_FONT_RUNTIME_COMPOSITE_H
