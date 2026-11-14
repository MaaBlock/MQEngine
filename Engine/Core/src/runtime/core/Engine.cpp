#include <MQEngine/Engine.h>
#include <MQEngine/runtime/core/CreateApplication.h>
#include <algorithm>

namespace MQEngine
{
    CreateApplicationFn CreateApplication = nullptr;

    FCT::MemoryTracker* GetSharedMemoryTracker() noexcept
    {
        return &FCT::MemoryTracker::localInstance();
    }

    const char* getEngineVersion()
    {
        return "2.0.0";
    }

    Runtime::Engine* getEngine()
    {
        return Runtime::Engine::getInstance();
    }
} // namespace MQEngine

namespace FCT
{
    std::string LoadStringFromStringResource(const unsigned char* resource, size_t size)
    {
        return std::string(reinterpret_cast<const char*>(resource), size);
    }
} // namespace FCT

namespace MQEngine
{
    namespace Runtime
    {
        Engine* Engine::s_instance = nullptr;

        Engine::Engine() = default;
        Engine::~Engine() = default;

        void Engine::initializeActiveApplicationTags()
        {
            m_activeApplicationTags.clear();
            if (application && !application->applicationRegisterInfo.tags.empty())
            {
                for (const std::string& tag : application->applicationRegisterInfo.tags)
                {
                    if (!tag.empty() && !hasApplicationTag(tag))
                    {
                        m_activeApplicationTags.push_back(tag);
                    }
                }
            }
            if (m_activeApplicationTags.empty())
            {
                m_activeApplicationTags.push_back("Runtime");
            }
        }

        bool Engine::hasApplicationTag(std::string_view tag) const noexcept
        {
            return std::find(m_activeApplicationTags.begin(), m_activeApplicationTags.end(), tag) !=
                   m_activeApplicationTags.end();
        }

        Status Engine::addApplicationTag(const std::string& tag)
        {
            if (tag.empty())
            {
                return InvalidArgumentError("Application Tag 不能为空");
            }
            if (hasApplicationTag(tag))
            {
                return OkStatus();
            }

            m_activeApplicationTags.push_back(tag);

            Status domainSt = domain.reconcileApplicationTags(m_activeApplicationTags);
            if (!domainSt.ok())
            {
                std::erase(m_activeApplicationTags, tag);
                (void)domain.reconcileApplicationTags(m_activeApplicationTags);
                return domainSt;
            }

            if (auto* compositeMgr = asset.getCompositeManager())
            {
                Status compSt = compositeMgr->reconcileActiveTags(m_activeApplicationTags);
                if (!compSt.ok())
                {
                    std::erase(m_activeApplicationTags, tag);
                    (void)compositeMgr->reconcileActiveTags(m_activeApplicationTags);
                    (void)domain.reconcileApplicationTags(m_activeApplicationTags);
                    return compSt;
                }
            }

            return OkStatus();
        }

        void Engine::removeApplicationTag(const std::string& tag) noexcept
        {
            if (tag.empty() || !hasApplicationTag(tag))
            {
                return;
            }

            std::erase(m_activeApplicationTags, tag);

            if (auto* compositeMgr = asset.getCompositeManager())
            {
                (void)compositeMgr->reconcileActiveTags(m_activeApplicationTags);
            }
            (void)domain.reconcileApplicationTags(m_activeApplicationTags);
        }
    } // namespace Runtime
} // namespace MQEngine
