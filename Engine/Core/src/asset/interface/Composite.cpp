/**
 * @file Composite.cpp
 * @brief Composite 基类默认实现
 */
#include "Composite.h"

#include <algorithm>
#include <unordered_set>

namespace MQEngine
{
    namespace Asset
    {
        Status Composite::onLoad(Runtime::Engine* engine)
        {
            m_engine = engine;
            m_loaded = true;
            return OkStatus();
        }

        Status Composite::onInit(Runtime::Engine* engine)
        {
            assert(m_loaded && "Composite 必须在 onLoad 成功后方可调用 onInit");
            m_engine = engine;
            m_initialized = true;
            return OkStatus();
        }

        void Composite::onTick(float deltaTime)
        {
            (void)deltaTime;
        }

        void Composite::onUnload()
        {
            m_initialized = false;
            m_loaded = false;
            m_engine = nullptr;
        }

        bool Composite::matchTags(const std::vector<std::string>& activeTags) const
        {
            const auto required = getRequiredTags();
            if (required.empty())
            {
                return true;
            }

            std::unordered_set<std::string> activeSet(activeTags.begin(), activeTags.end());
            for (const auto& tag : required)
            {
                if (!activeSet.contains(tag))
                {
                    return false;
                }
            }
            return true;
        }

    } // namespace Asset
} // namespace MQEngine