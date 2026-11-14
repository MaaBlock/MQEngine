/**
 * @file CompositeManager.h
 * @brief Composite 资产与插件拓扑调度管理器
 */
#ifndef MQENGINE_ASSET_MANAGER_COMPOSITEMANAGER_H
#define MQENGINE_ASSET_MANAGER_COMPOSITEMANAGER_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/classdef.h>
#include <MQEngine/asset/interface/Composite.h>
#include <MQEngine/asset/interface/Plugin.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace MQEngine
{
    namespace Asset
    {
        /**
         * @brief CompositeManager:
         *        管理所有 PluginPackage 与 Composite，
         *        根据 Application tags 过滤激活项，基于 FCT::TokenGraph 进行拓扑排序，
         *        并按正序 onLoad/onInit/onTick、逆序 onUnload 驱动全生命周期。
         */
        class ENGINE_API CompositeManager : public CoreEngineManager<CompositeManager>
        {
        public:
            CompositeManager();
            ~CompositeManager();

            CompositeManager(const CompositeManager&) = delete;
            CompositeManager& operator=(const CompositeManager&) = delete;

            void registerInfo();
            Status init();
            void term() noexcept;

            /**
             * @brief 移交并清空所有已析构动态库 Composite 延迟卸载的原生句柄，供 Runtime 在引擎完全销毁后统一释放
             */
            std::vector<void*> takeDeferredLibraryHandles() noexcept;

            /**
             * @brief 按卸载顺序安全释放动态库模块句柄
             */
            static void closeLibraryHandles(const std::vector<void*>& handles) noexcept;

            // ==================== 1. 注册与装载 ====================

            /**
             * @brief 注册一个已实例化的 Composite
             */
            Status registerComposite(UniquePtr<Composite> composite);

            /**
             * @brief 注册一个插件包
             */
            Status registerPluginPackage(PluginPackage plugin);

            /**
             * @brief 扫描并发现指定目录下的插件包（读取 plugin.mq）
             * @param pluginsDir 插件根目录（如 ./Plugins 或 Engine/Plugins）
             */
            Status discoverPlugins(const std::string& pluginsDir);

            /**
             * @brief 自动基于 Resource::Config 规范路径扫描默认引擎与工程插件目录
             */
            Status discoverDefaultPlugins();

            /**
             * @brief 扫描工程 Native 目录并探测导出 CreateNativeGameModule 的主游戏模块
             * @param nativeDir 工程 Native 目录（通常由 Resource::Config::getProjectNativeDir() 提供）
             */
            Status discoverProjectNativeModule(const std::string& nativeDir);

            /**
             * @brief 读取当前 Runtime::Engine 的 Application tags 并执行全量对账加载与初始化
             */
            Status activateApplicationComposites();

            /**
             * @brief 根据当前活跃 Application tags 执行增量对称生命周期对账：
             *        停用不再匹配的组件（拓扑逆序 onUnload -> GPU 排空 -> 卸载 DLL），
             *        并仅对新激活的组件按拓扑正序执行 onLoad / onInit
             */
            Status reconcileActiveTags(const std::vector<std::string>& activeTags);

            /**
             * @brief 注册 LogicHost Ticker 节点驱动 CompositeManager::tick
             */
            void registerLogicTicker();

            /**
             * @brief 注销 LogicHost Ticker 节点
             */
            void unregisterLogicTicker() noexcept;

            // ==================== 2. 拓扑排序与生命周期 ====================

            /**
             * @brief 根据宿主应用的 activeTags 过滤 Composite 并建立 TokenGraph 拓扑排序
             * @param activeTags 宿主应用激活的 Tags 集合（如 {"Runtime", "Editor", "ImGui"}）
             */
            Status buildTopologicalSchedule(const std::vector<std::string>& activeTags);

            /**
             * @brief 装载全部已排序的活跃 Composite（正序调用 onLoad）
             */
            Status loadComposites(Runtime::Engine* engine, const std::vector<std::string>& activeTags);

            /**
             * @brief 初始化全部活跃 Composite（正序调用 onInit）
             */
            Status initComposites(Runtime::Engine* engine);

            /**
             * @brief 驱动每帧 Tick 更新（正序调用 onTick）
             */
            void tick(float deltaTime);

            /**
             * @brief 逆序卸载全部 Composite（逆序调用 onUnload）
             */
            void unloadComposites();

            /**
             * @brief 单个 Composite 热重载
             */
            Status reloadComposite(const std::string& name);

            // ==================== 3. 查询接口 ====================

            Composite* getComposite(const std::string& name) const;

            template <typename T>
            T* getComposite() const
            {
                for (const auto& [name, comp] : m_allComposites)
                {
                    if (auto casted = dynamic_cast<T*>(comp.get()))
                    {
                        return casted;
                    }
                }
                return nullptr;
            }

            const std::vector<Composite*>& getActiveExecutionOrder() const noexcept
            {
                return m_executionOrder;
            }

            size_t getRegisteredCompositeCount() const noexcept { return m_allComposites.size(); }
            size_t getActiveCompositeCount() const noexcept { return m_executionOrder.size(); }
            size_t getPluginCount() const noexcept { return m_plugins.size(); }

            const std::unordered_map<std::string, CompositeManifest>& getCompositeManifests() const noexcept
            {
                return m_compositeManifests;
            }

            const std::unordered_map<std::string, PluginPackage>& getPlugins() const noexcept
            {
                return m_plugins;
            }

        private:
            void clearTopologicalOrder();
            bool matchesCompositeTags(const std::string& name,
                                      const Composite* comp,
                                      const std::vector<std::string>& activeTags) const;
            std::vector<std::string> collectCompositeDependencies(const std::string& name,
                                                                  const Composite* comp) const;
            Status loadMatchedCompositeDlls(const std::vector<std::string>& activeTags,
                                            std::vector<std::string>* outNewlyLoadedNames = nullptr);
            void deactivateStaleComposites(const std::vector<std::string>& activeTags,
                                           Runtime::Engine* engine) noexcept;
            void releaseDynamicCompositesAndLibraries(const std::vector<std::string>& names,
                                                      Runtime::Engine* engine) noexcept;
            void registerNativeDllSearchDirectory(const std::string& dir);
            void clearNativeDllSearchDirectories() noexcept;
            void deferLibraryHandle(void* handle) noexcept;

        private:
            Runtime::Engine* m_engine = nullptr;
            std::vector<std::string> m_cachedActiveTags;

            // 存储所有已注册的 Composite 实例
            std::unordered_map<std::string, UniquePtr<Composite>> m_allComposites;

            // 存储所有已发现但按需延迟 LoadLibrary 的 Composite DLL 清单
            std::unordered_map<std::string, CompositeManifest> m_compositeManifests;

            // 存储所有已注册的插件包元信息与容器
            std::unordered_map<std::string, PluginPackage> m_plugins;

            // 经过 Tag 过滤和 FCT::TokenGraph 拓扑排序后的正向执行列表
            std::vector<Composite*> m_executionOrder;

            // 帧更新期间复用的名称快照缓冲，防止 onTick 内动态切换 Tag 导致迭代器失效
            std::vector<std::string> m_tickNameScratch;

            // FCT TokenGraph 调度图
            TokenGraph<std::string, Composite*> m_tokenGraph;

            std::vector<std::string> m_registeredNativeDllDirKeys;
            std::vector<void*> m_nativeDllDirCookies;
            // 暂存已析构动态库 Composite 对应的原生句柄，延迟至 Runtime 销毁 m_engine 后再统一释放
            std::vector<void*> m_deferredUnloadHandles;
            bool m_initialized = false;
        };

    } // namespace Asset
} // namespace MQEngine

#endif // MQENGINE_ASSET_MANAGER_COMPOSITEMANAGER_H
