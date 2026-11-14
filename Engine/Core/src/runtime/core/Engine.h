#ifndef MQENGINE_RUNTIME_CORE_ENGINE_H
#define MQENGINE_RUNTIME_CORE_ENGINE_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/host/core/Host.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/asset/core/Asset.h>
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/runtime/classdef.h>
#include <MQEngine/runtime/core/CreateApplication.h>
#include <string>
#include <string_view>
#include <vector>

namespace MQEngine
{
    namespace Runtime
    {
        /**
         * @brief 引擎顶层模块聚合数据容器，按值内嵌 (In-place Value Embedding) 全部核心模块实例
         *        （Resource, Host, Asset, Domain）；
         *        消除运行时指针间接寻址开销，生命周期由友元 Runtime 统一驱动管控。
         */
        class ENGINE_API Engine
        {
        private:
            friend class Runtime;
            static Engine* s_instance;

        public:
            Engine();
            ~Engine();
            Engine(const Engine&) = delete;
            Engine& operator=(const Engine&) = delete;
            Engine(Engine&&) = delete;
            Engine& operator=(Engine&&) = delete;

            static Engine* getInstance() noexcept
            {
                return s_instance;
            }

            /**
             * @brief 获取当前活跃的 Application Tags 集合（支持编辑器/运行时动态切换）
             */
            const std::vector<std::string>& getActiveApplicationTags() const noexcept
            {
                return m_activeApplicationTags;
            }

            bool hasApplicationTag(std::string_view tag) const noexcept;
            Status addApplicationTag(const std::string& tag);
            void removeApplicationTag(const std::string& tag) noexcept;

            Application* application = nullptr;
            Resource::Resource resource;
            Host::Host host;
            Asset::Asset asset;
            Domain::Domain domain;

        private:
            void initializeActiveApplicationTags();
            std::vector<std::string> m_activeApplicationTags;
        };

    } // namespace Runtime

    ENGINE_API const char* getEngineVersion();
    ENGINE_API Runtime::Engine* getEngine();
} // namespace MQEngine

#endif // MQENGINE_RUNTIME_CORE_ENGINE_H
