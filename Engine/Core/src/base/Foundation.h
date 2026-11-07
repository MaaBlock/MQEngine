#ifndef MQENGINE_BASE_FOUNDATION_H
#define MQENGINE_BASE_FOUNDATION_H

#include <string>
#include <unordered_map>
#include <vector>
#include "../EnginePCH.h"
#include "../scene/system/ComponentReflection.h"
#include "../thirdparty/thirdparty.h"
#include "./ConfigDocument.h"
#include "./CoreEngineModule.h"
#include "./EngineCLIOption.h"
#include "./JobSystem.h"
#include "./MemoryManager.h"
#include "./classdef.h"
#include "./data/EngineCoreConfig.h"

namespace MQEngine
{
    class ResourceLoader;

    /**
     * @brief 工程与引擎配置数据结构（原 ProjectConfig 下沉至 Base 层统一管理）
     */
    struct ProjectConfig
    {
        std::string projectName;
        std::string version = "1.0.0";
        std::string projectFilePath;
        std::string projectDirectory;
        std::string assetRoot = "Assets";
        std::string resolvedAssetRoot;
        std::string pipelinePath;
        std::string resolvedPipelinePath;
        std::vector<std::string> plugins;
        std::string userCode = "UserCode";
        std::vector<std::string> userCodes;
        std::string initialSceneUuid;
        std::unordered_map<std::string, std::string> settings;
    };

    namespace Base
    {
        using MQEngine::Status;
        using MQEngine::StatusOr;

        /**
         * @brief Base 模块顶层门面类（Foundation），统管引擎底层基础设施：
         *        内存管理（MemoryManager）、多线程任务系统（JobSystem）、命令行职责链解析（EngineCLIOption）、
         *        引擎与工程配置加载启用（ProjectConfig）、组件反射注册与初始化（ComponentReflection / EnTT Storage）及本地化环境
         */
        class ENGINE_API Foundation : public CoreEngineModule<Foundation>
        {
        public:
            static constexpr const char* kDefaultProjectFileName = "project.mq";
            static constexpr const char* kLegacyProjectFileName = "project.json";

            Foundation();
            ~Foundation();

            Foundation(const Foundation&) = delete;
            Foundation& operator=(const Foundation&) = delete;

            /**
             * @brief 注册阶段（registerModules）调用，注册子模块元信息、反射信息、CLI职责链与全局句柄
             */
            void registerInfo();

            /**
             * @brief 初始化底层基础设施（解析命令行、对齐工作目录、加载并启用工程配置、本地化、无头异常弹窗抑制、反射初始化与 EnTT 组件池预注册）
             */
            Status init();

            /**
             * @brief 逻辑线程帧首调用：翻转双缓冲帧内存池并排空主线程待办任务队列
             */
            void beginLogicFrame();

            /**
             * @brief 关闭线程池并释放底层基础设施资源
             */
            void term();

            MemoryManager* getMemoryManager() const noexcept { return const_cast<MemoryManager*>(&memoryManager); }
            JobSystem* getJobSystem() const noexcept { return const_cast<JobSystem*>(&jobSystem); }
            EngineCLIOption* getCLIOption() const noexcept { return const_cast<EngineCLIOption*>(&cliOption); }
            ComponentReflection* getComponentReflection() const noexcept { return const_cast<ComponentReflection*>(&componentReflection); }
            EngineConfig* getEngineConfig() const noexcept { return const_cast<EngineConfig*>(&engineConfig); }

            int32_t getTestFrames() const noexcept { return engineConfig.testFrames; }
            void setTestFrames(int32_t frames) noexcept { engineConfig.testFrames = (frames > 0) ? frames : 0; }
            bool hasTestFramesLimit() const noexcept { return engineConfig.hasTestFramesLimit(); }

            static void addLogSink(const std::shared_ptr<spdlog::sinks::sink>& sink);
            static void removeLogSink(const std::shared_ptr<spdlog::sinks::sink>& sink);

            // ==================== 工程与引擎配置管理 ====================
            /**
             * @brief 读取并解析工程配置文件 (project.mq)
             */
            StatusOr<ProjectConfig> loadProjectConfig(const std::string& projectFilePath,
                                                      ResourceLoader* loader = nullptr);

            /**
             * @brief 将工程配置序列化并保存至指定路径
             */
            Status saveProjectConfig(const std::string& projectFilePath,
                                     const ProjectConfig& config,
                                     ResourceLoader* loader = nullptr);

            /**
             * @brief 加载并启用指定路径的工程配置作为当前活跃配置
             */
            Status activateProjectConfig(const std::string& projectFilePath,
                                         ResourceLoader* loader = nullptr);

            /**
             * @brief 清空当前活跃工程配置状态
             */
            void clearProjectConfig();

            bool isProjectOpen() const noexcept { return m_isProjectOpen; }
            const ProjectConfig& getCurrentProject() const noexcept { return m_currentConfig; }
            const ProjectConfig& getCurrentConfig() const noexcept { return m_currentConfig; }
            const std::string& getProjectDirectory() const noexcept { return m_currentConfig.projectDirectory; }
            const std::string& getAssetRoot() const noexcept { return m_currentConfig.resolvedAssetRoot; }
            std::string getProjectName() const noexcept { return m_isProjectOpen ? m_currentConfig.projectName : ""; }

        public:
            MemoryManager memoryManager;
            JobSystem jobSystem;
            EngineCLIOption cliOption;
            ComponentReflection componentReflection;
            EngineConfig engineConfig;

        private:
            void registerEnttComponents();
            ResourceLoader* resolveLoader(ResourceLoader* explicitLoader);
            Status parseProjectConfigDocument(const ConfigDocument& doc,
                                              const std::string& projectFilePath,
                                              ProjectConfig& outConfig);
            void populateProjectIdentity(const ConfigDocument& doc,
                                         const std::string& projectFilePath,
                                         ProjectConfig& outConfig) const;
            void populateProjectPaths(const ConfigDocument& doc, ProjectConfig& outConfig) const;
            void populateProjectModules(const ConfigDocument& doc, ProjectConfig& outConfig) const;
            void buildProjectConfigDocument(const ProjectConfig& config, ConfigDocument& outDoc) const;

            ProjectConfig m_currentConfig;
            bool m_isProjectOpen = false;
            bool m_initialized = false;
        };

    } // namespace Base
} // namespace MQEngine

#endif // MQENGINE_BASE_FOUNDATION_H
