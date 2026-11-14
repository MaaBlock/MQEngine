/**
 * @file FontRuntimeComposite.cpp
 * @brief Font 插件运行时 Composite 生命周期实现与无锁全局解析入口
 */
#include "FontRuntimeComposite.h"

#include <MQEngine/asset/core/Asset.h>
#include <MQEngine/asset/manager/CompositeManager.h>
#include <MQEngine/host/render/RenderHost.h>
#include <MQEngine/runtime/core/Engine.h>

namespace MQEngine
{
    namespace Font
    {
        namespace
        {
            // 采用原子指针维护活动实例，避免在字体查询热路径引入互斥锁
            std::atomic<FontRuntimeComposite*> g_activeFontComposite{nullptr};
        }

        FontRuntimeComposite::FontRuntimeComposite() = default;

        FontRuntimeComposite::~FontRuntimeComposite()
        {
            onUnload();
        }

        Status FontRuntimeComposite::onLoad(Runtime::Engine* engine)
        {
            ZoneScoped();
            CHECK_STATUS(Asset::Composite::onLoad(engine));

            if (!m_fontSystem)
            {
                m_fontSystem = makeUnique<Resource::Font::FontSystem>();
            }
            if (auto* renderHost = Host::RenderHost::InstancePtr(); renderHost && renderHost->getContext())
            {
                m_fontSystem->setDeferGpuUpload(true);
            }
            if (!m_fontSystem->isInitialized())
            {
                CHECK_STATUS(m_fontSystem->init());
            }

            g_activeFontComposite.store(this, std::memory_order_release);
            return OkStatus();
        }

        Status FontRuntimeComposite::onInit(Runtime::Engine* engine)
        {
            ZoneScoped();
            CHECK_STATUS(Asset::Composite::onInit(engine));

            if (!m_fontSystem)
            {
                m_fontSystem = makeUnique<Resource::Font::FontSystem>();
            }
            if (auto* renderHost = Host::RenderHost::InstancePtr(); renderHost && renderHost->getContext())
            {
                m_fontSystem->setDeferGpuUpload(true);
            }
            if (!m_fontSystem->isInitialized())
            {
                CHECK_STATUS(m_fontSystem->init());
            }

            g_activeFontComposite.store(this, std::memory_order_release);
            return OkStatus();
        }

        void FontRuntimeComposite::onTick(float deltaTime)
        {
            ZoneScoped();
            (void)deltaTime;
        }

        void FontRuntimeComposite::onUnload()
        {
            ZoneScoped();
            FontRuntimeComposite* expected = this;
            g_activeFontComposite.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);

            if (m_fontSystem)
            {
                m_fontSystem->term();
                m_fontSystem.reset();
            }

            Asset::Composite::onUnload();
        }

        Resource::Font::FontSystem* FontRuntimeComposite::getFontSystem() noexcept
        {
            return m_fontSystem.get();
        }

        const Resource::Font::FontSystem* FontRuntimeComposite::getFontSystem() const noexcept
        {
            return m_fontSystem.get();
        }

        FontRuntimeComposite* FontRuntimeComposite::InstancePtr() noexcept
        {
            return g_activeFontComposite.load(std::memory_order_acquire);
        }

        Resource::Font::FontSystem* FontRuntimeComposite::getActiveFontSystem() noexcept
        {
            if (FontRuntimeComposite* active = g_activeFontComposite.load(std::memory_order_acquire))
            {
                if (Resource::Font::FontSystem* fontSys = active->getFontSystem())
                {
                    return fontSys;
                }
            }

            if (auto* assetMod = Asset::Asset::InstancePtr())
            {
                if (auto* baseComp = assetMod->compositeManager.getComposite("FontRuntimeComposite"))
                {
                    if (auto* fontComp = dynamic_cast<FontRuntimeComposite*>(baseComp))
                    {
                        return fontComp->getFontSystem();
                    }
                }
            }
            return nullptr;
        }
    } // namespace Font
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(::MQEngine::Font::FontRuntimeComposite)
