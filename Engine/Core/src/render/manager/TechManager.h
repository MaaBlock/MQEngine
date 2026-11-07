#ifndef ENGINE_TECHMANAGER_H
#define ENGINE_TECHMANAGER_H
#include "../../base/CoreEngineManager.h"
#include "../../base/SingleQueueEventSystem.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../core/tech.h"
#include "./PipelineConfig.h"
#include "../../scene/manager/DataManager.h"
#include <unordered_map>
#include <memory>
#include <mutex>

namespace MQEngine
{
    class ENGINE_API TechManager : public CoreEngineManager<TechManager>
    {
    public:
        struct Request
        {
            struct AddTech
            {
                std::string passName;
                Tech tech;
            };
            struct AddTechDesc
            {
                PipelineTechDesc techDesc;
            };
            struct BuildPipeline
            {
                PipelineConfig config;
            };
        };

        TechManager();
        ~TechManager();

        void registerInfo() {}
        void init(Context* ctx, DataManager* dataManager);
        void term();

        void setReverseZ(bool reverseZ);
        bool isReverseZ() const { return m_reverseZ; }

        Status addTech(const std::string& passName, Tech&& tech);
        StatusOr<Tech> buildTech(const PipelineTechDesc& techDesc) const;
        Status buildFromPipeline(const PipelineConfig& config);

        void requestAddTech(const std::string& passName, Tech&& tech);
        void requestAddTechDesc(const PipelineTechDesc& techDesc);
        void requestBuildFromPipeline(const PipelineConfig& config);

        /**
         * @brief 线程汇聚点（及 loop 前）统一消费挂起的 Technique 构建与注册请求
         */
        void syncTicker();
        void flushRequests() { syncTicker(); }

        const std::vector<Tech*>& getTechsForPass(const std::string& passName);

        FCT::Layout* getLayoutForTech(const std::string& techName);
        /**
         * @brief 为指定Pass订阅渲染事件和PassInfo更新
         * @param passName Pass名称
         */
        Status subscribeToPass(const std::string& passName);

    private:
        void ensureInitialized();

        struct LayoutKey
        {
            std::string techName;
            std::string passName;
            size_t vertexLayoutHash;
            size_t pixelLayoutHash;

            bool operator<(const LayoutKey& other) const
            {
                if (techName < other.techName) return true;
                if (techName > other.techName) return false;
                if (passName < other.passName) return true;
                if (passName > other.passName) return false;
                if (vertexLayoutHash < other.vertexLayoutHash) return true;
                if (vertexLayoutHash > other.vertexLayoutHash) return false;
                return pixelLayoutHash < other.pixelLayoutHash;
            }
        };

        Context* m_ctx = nullptr;
        std::unordered_map<std::string, UniquePtr<Tech>> m_techs;
        std::map<std::string, std::vector<Tech*>> m_passTechs;
        std::map<LayoutKey, UniquePtr<FCT::Layout>> m_layouts;
        std::map<std::string, FCT::Layout*> m_techToLayoutMap;
        std::set<std::string> m_subscribedPasses;
        std::map<std::string, FCT::OutputInfo> m_passOutputInfos;
        DataManager* m_dataManager = nullptr;
        std::map<std::string, FCT::RHI::Pass*> m_rhiPasses;
        FCT::DepthStencilState* m_defaultDepthStencilState = nullptr;
        FCT::DepthStencilState* m_reverseZDepthStencilState = nullptr;
        bool m_reverseZ = true;
        mutable std::mutex m_requestMutex;
        SingleQueueEventSystem m_requestQueue;
    };
}
#endif
