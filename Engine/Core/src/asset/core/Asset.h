/**
 * @file Asset.h
 * @brief Asset 模块顶层门面类（CoreEngineModule<Asset>）
 */
#ifndef MQENGINE_ASSET_CORE_ASSET_H
#define MQENGINE_ASSET_CORE_ASSET_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/classdef.h>
#include <MQEngine/asset/interface/Composite.h>
#include <MQEngine/asset/interface/Plugin.h>
#include <MQEngine/asset/manager/CompositeManager.h>
#include <MQEngine/asset/loader/AsyncLoader.h>
#include <MQEngine/asset/loader/AsyncTextureUploader.h>

namespace MQEngine
{
    namespace Asset
    {
        /**
         * @brief Asset 模块顶层门面类：
         *        按值内嵌于 Runtime::Engine，管理异步文件与网络 I/O、异步纹理上传、
         *        以及基于 TokenGraph 拓扑调度的 Composite 插件系统。
         */
        class ENGINE_API Asset : public CoreEngineModule<Asset>
        {
        public:
            Asset();
            ~Asset();

            Asset(const Asset&) = delete;
            Asset& operator=(const Asset&) = delete;

            void registerInfo();
            Status init();
            void term() noexcept;

            CompositeManager* getCompositeManager() const noexcept { return const_cast<CompositeManager*>(&compositeManager); }
            AsyncLoader* getAsyncLoader() const noexcept { return const_cast<AsyncLoader*>(&asyncLoader); }
            AsyncTextureUploader* getTextureUploader() const noexcept { return const_cast<AsyncTextureUploader*>(&textureUploader); }

            /**
             * @brief 注册一个 Composite 资产单元
             */
            Status registerComposite(UniquePtr<Composite> composite)
            {
                return compositeManager.registerComposite(std::move(composite));
            }

            /**
             * @brief 注册一个插件包
             */
            Status registerPluginPackage(PluginPackage plugin)
            {
                return compositeManager.registerPluginPackage(std::move(plugin));
            }

            /**
             * @brief 触发异步纹理上传
             */
            Status uploadTextureAsync(TextureUploadDesc desc, AsyncTextureUploader::UploadCallback onComplete)
            {
                return textureUploader.uploadTextureAsync(std::move(desc), std::move(onComplete));
            }

        public:
            CompositeManager compositeManager;
            AsyncLoader asyncLoader;
            AsyncTextureUploader textureUploader;

        private:
            bool m_initialized = false;
        };

    } // namespace Asset
} // namespace MQEngine

#endif // MQENGINE_ASSET_CORE_ASSET_H
