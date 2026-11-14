/**
 * @file SceneTrunk.h
 * @brief 分块子场景 (SceneTrunk)：
 *        支持场景分块持久化存储、异步流式反序列化加载与整块挂载/卸载
 */
#ifndef MQENGINE_DOMAIN_SCENE_SCENETRUNK_H
#define MQENGINE_DOMAIN_SCENE_SCENETRUNK_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <string>
#include <memory>

namespace MQEngine
{
    namespace Domain
    {
        namespace Scene
        {
            /**
             * @brief 分块子场景核心类
             */
            class ENGINE_API SceneTrunk
            {
            public:
                explicit SceneTrunk(std::string name);
                ~SceneTrunk();

                SceneTrunk(const SceneTrunk&) = delete;
                SceneTrunk& operator=(const SceneTrunk&) = delete;

                const std::string& getName() const noexcept { return m_name; }

                Status load(const std::string& directoryPath);
                Status save(const std::string& directoryPath);

                bool isLoaded() const noexcept { return m_loaded; }
                bool isVisible() const noexcept { return m_visible; }
                void setVisible(bool visible) noexcept { m_visible = visible; }

                FCT::ECS::registry& getRegistry() noexcept { return *m_registry; }
                const FCT::ECS::registry& getRegistry() const noexcept { return *m_registry; }

            private:
                std::string m_name;
                bool m_loaded = false;
                bool m_visible = true;
                UniquePtr<FCT::ECS::registry> m_registry;
            };

        } // namespace Scene
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_SCENE_SCENETRUNK_H
