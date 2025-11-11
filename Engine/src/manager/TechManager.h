#ifndef ENGINE_TECHMANAGER_H
#define ENGINE_TECHMANAGER_H
#include "../core/EngineGlobal.h"
#include "../core/tech.h"
#include "../data/DataManager.h"
#include <unordered_map>
#include <memory>

namespace MQEngine
{
    class TechManager
    {
    public:
        TechManager();

        Status addTech(const std::string& passName, Tech&& tech);

        const std::vector<Tech*>& getTechsForPass(const std::string& passName);

        FCT::Layout* getLayoutForTech(const std::string& techName);
        /**
         * @brief 为指定Pass订阅渲染事件和PassInfo更新
         * @param passName Pass名称
         */
        Status subscribeToPass(const std::string& passName);



    private:
        struct LayoutKey
        {
            std::string passName;
            size_t vertexLayoutHash;
            size_t pixelLayoutHash;

            bool operator<(const LayoutKey& other) const
            {
                if (passName < other.passName) return true;
                if (passName > other.passName) return false;
                if (vertexLayoutHash < other.vertexLayoutHash) return true;
                if (vertexLayoutHash > other.vertexLayoutHash) return false;
                return pixelLayoutHash < other.pixelLayoutHash;
            }
        };

        Context* m_ctx;
        std::unordered_map<std::string, std::unique_ptr<Tech>> m_techs;
        std::map<std::string, std::vector<Tech*>> m_passTechs;
        std::map<LayoutKey, std::unique_ptr<FCT::Layout>> m_layouts;
        std::map<std::string, FCT::Layout*> m_techToLayoutMap;
        std::set<std::string> m_subscribedPasses;  // 跟踪已订阅的Pass
        std::map<std::string, FCT::OutputInfo> m_passOutputInfos;  // 存储各Pass的输出信息
        DataManager* m_dataManager;
    };
}
#endif