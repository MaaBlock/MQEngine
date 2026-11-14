/**
 * @file Plugin.h
 * @brief 插件包元数据与逻辑容器定义
 */
#ifndef MQENGINE_ASSET_INTERFACE_PLUGIN_H
#define MQENGINE_ASSET_INTERFACE_PLUGIN_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/classdef.h>
#include <MQEngine/asset/interface/Composite.h>

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace MQEngine
{
    namespace Asset
    {
        /**
         * @brief 插件包逻辑容器：
         *        管理 plugin.mq 元数据以及注册在插件内的 Composite 工厂
         */
        class ENGINE_API PluginPackage
        {
        public:
            using CompositeFactory = std::function<UniquePtr<Composite>()>;

            PluginPackage() = default;
            explicit PluginPackage(PluginMetadata metadata);
            ~PluginPackage();

            PluginPackage(const PluginPackage&) = delete;
            PluginPackage& operator=(const PluginPackage&) = delete;

            PluginPackage(PluginPackage&& other) noexcept;
            PluginPackage& operator=(PluginPackage&& other) noexcept;

            const PluginMetadata& getMetadata() const noexcept { return m_metadata; }
            PluginMetadata& getMutableMetadata() noexcept { return m_metadata; }
            const std::string& getName() const noexcept { return m_metadata.name; }

            /**
             * @brief 注册该插件包内的一个 Composite 工厂
             */
            void registerCompositeFactory(const std::string& compositeName, CompositeFactory factory);

            /**
             * @brief 创建该插件包下声明的全部 Composite 实例
             */
            std::vector<UniquePtr<Composite>> createComposites() const;

            /**
             * @brief 解析 plugin.mq (JSON格式) 内容并填充元数据
             */
            static StatusOr<PluginMetadata> parseMetadataFromJson(const std::string& jsonContent);

            /**
             * @brief 解析 plugin.mq (TOML格式) 内容并填充元数据
             */
            static StatusOr<PluginMetadata> parseMetadataFromToml(const std::string& tomlContent);

            /**
             * @brief 自动检测并解析 plugin.mq (支持 TOML 与 JSON 格式)
             */
            static StatusOr<PluginMetadata> parseMetadata(const std::string& content);

        private:
            PluginMetadata m_metadata;
            std::unordered_map<std::string, CompositeFactory> m_factories;
        };

    } // namespace Asset
} // namespace MQEngine

#endif // MQENGINE_ASSET_INTERFACE_PLUGIN_H
