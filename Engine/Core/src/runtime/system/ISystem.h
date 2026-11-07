//
// Created by MaaBlock on 2025/10/25.
//

#ifndef ISYSTEM_H
#define ISYSTEM_H
#include <array>
#include "../core/EngineGlobal.h"
namespace MQEngine {
    class ENGINE_API ISystem
    {
    public:
        virtual ~ISystem() = default;
        /**
         * @brief 获取后继系统
         * @return 后继系统
         */
        virtual std::vector<std::string> getSuccessors()
        {
            return {};
        }
        /**
         * @brief 获取依赖系统
         * @return 依赖系统
         */
        virtual std::vector<std::string> getPredecessors()
        {
            return {};
        }
        /**
         * @brief 逻辑线程更新
         */
        virtual void updateLogic() = 0;
        /**
         * @brief 渲染线程更新
         */
        virtual void updateRender() = 0;
        /**
         * @brief 系统激活时调用
         */
        virtual void onActivate() {}
        /**
         * @brief 系统停用时调用
         */
        virtual void onDeactivate() {}
    };
} // MQEngine

#endif //ISYSTEM_H
