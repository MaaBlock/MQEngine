/**
 * @file Composite.h
 * @brief 插件功能资产单元基类（Composite）
 */
#ifndef MQENGINE_ASSET_INTERFACE_COMPOSITE_H
#define MQENGINE_ASSET_INTERFACE_COMPOSITE_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/classdef.h>

#include <string>
#include <vector>

namespace MQEngine
{
    namespace Asset
    {
        /**
         * @brief 最小功能资产拓扑单元基类：
         *        所有具体业务组件与编辑器功能面板统一继承此类，
         *        通过依赖声明参与 TokenGraph 拓扑排序，按 Application tags 过滤并受 CompositeManager 生命周期管控。
         */
        class ENGINE_API Composite
        {
        public:
            Composite() = default;
            virtual ~Composite() = default;

            Composite(const Composite&) = delete;
            Composite& operator=(const Composite&) = delete;

            /**
             * @brief 资产装载阶段（建立数据连接与基础上下文）
             * @param engine 引擎全局容器实例
             */
            virtual Status onLoad(Runtime::Engine* engine);

            /**
             * @brief 初始化阶段（在依赖项均已完成 onLoad 与 onInit 后调用）
             * @param engine 引擎全局容器实例
             */
            virtual Status onInit(Runtime::Engine* engine);

            /**
             * @brief 每帧逻辑或渲染驱动更新
             * @param deltaTime 帧时间（秒）
             */
            virtual void onTick(float deltaTime);

            /**
             * @brief 卸载与清理阶段（按拓扑逆序释放资源）
             */
            virtual void onUnload();

            /**
             * @brief 获取 Composite 唯一名称标识
             */
            virtual std::string getName() const = 0;

            /**
             * @brief 所属 Plugin 包名称（如 EditorBase、MQEditor、GameHtmlUI）
             */
            virtual std::string getPluginName() const { return ""; }

            /**
             * @brief 依赖的前置 Composite 名称列表（支持同 Plugin 内部与跨 Plugin 依赖）
             */
            virtual std::vector<std::string> getDependencies() const { return {}; }

            /**
             * @brief 激活本 Composite 所必须具备的宿主 Application 标签集合
             *        例如：{"Runtime", "ImGui"}、{"Editor", "ImGui"}、{"Runtime", "Game"}
             *        若为空，表示无条件激活
             */
            virtual std::vector<std::string> getRequiredTags() const { return {}; }

            /**
             * @brief 判断本 Composite 是否匹配当前宿主应用激活的 Tags
             * @param activeTags 宿主 Application 注入的激活标签列表
             * @return 若当前标签集合包含本 Composite 的全部 requiredTags，返回 true
             */
            bool matchTags(const std::vector<std::string>& activeTags) const;

            // 状态查询与管控
            bool isLoaded() const noexcept { return m_loaded; }
            bool isInitialized() const noexcept { return m_initialized; }
            Runtime::Engine* getEngine() const noexcept { return m_engine; }

            void setLoaded(bool loaded) noexcept { m_loaded = loaded; }
            void setInitialized(bool initialized) noexcept { m_initialized = initialized; }

            /**
             * @brief 设置/获取当前 Composite 对应的独立 DLL 模块句柄
             */
            void setModuleHandle(void* handle) noexcept { m_moduleHandle = handle; }
            void* getModuleHandle() const noexcept { return m_moduleHandle; }

        protected:
            Runtime::Engine* m_engine = nullptr;
            void* m_moduleHandle = nullptr;
            bool m_loaded = false;
            bool m_initialized = false;
        };

        /**
         * @brief 每个独立 Composite DLL 导出的标准工厂函数签名与符号名
         */
        using CreateCompositeFn = Composite* (*)();
        inline constexpr const char* kCreateCompositeSymbolName = "MQCreateComposite";

    } // namespace Asset
} // namespace MQEngine

#include <MQEngine/runtime/core/CreateApplication.h>

#ifdef _WIN32
#define MQ_EXPORT_COMPOSITE(CompositeClass) \
    extern "C" __declspec(dllexport) ::MQEngine::Asset::Composite* MQCreateComposite() \
    { \
        ::MQEngine::BindModuleSharedMemoryTracker(); \
        return FCT_NEW(CompositeClass); \
    }
#else
#define MQ_EXPORT_COMPOSITE(CompositeClass) \
    extern "C" __attribute__((visibility("default"))) ::MQEngine::Asset::Composite* MQCreateComposite() \
    { \
        ::MQEngine::BindModuleSharedMemoryTracker(); \
        return FCT_NEW(CompositeClass); \
    }
#endif

#endif // MQENGINE_ASSET_INTERFACE_COMPOSITE_H
