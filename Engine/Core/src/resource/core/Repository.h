#ifndef MQENGINE_RESOURCE_CORE_REPOSITORY_H
#define MQENGINE_RESOURCE_CORE_REPOSITORY_H

#include "../../EnginePCH.h"
#include "../../base/CoreEngineModule.h"
#include "../../thirdparty/thirdparty.h"
#include "../classdef.h"
#include "./DiskResourceLoader.h"
#include "../system/ResourceActiveSystem.h"

namespace MQEngine
{
    namespace Resource
    {
        class ENGINE_API Repository : public CoreEngineModule<Repository>
        {
        public:
            Repository();
            ~Repository();

            Repository(const Repository&) = delete;
            Repository& operator=(const Repository&) = delete;

            void registerInfo();
            Status init();
            void term();

            DiskResourceLoader* getResourceLoader() const noexcept { return const_cast<DiskResourceLoader*>(&resourceLoader); }
            ResourceActiveSystem* getResourceActiveSystem() const noexcept { return resourceActiveSystem.get(); }

            const std::string& getAssetRoot() const noexcept { return m_assetRoot; }
            void setAssetRoot(const std::string& assetRoot) { m_assetRoot = assetRoot; }

        public:
            DiskResourceLoader resourceLoader;
            UniquePtr<ResourceActiveSystem> resourceActiveSystem;

        private:
            std::string m_assetRoot = "./Assets";
            bool m_initialized = false;
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_CORE_REPOSITORY_H
