//
// Created by Administrator on 2025/8/31.
//

#ifndef SCRIPTSYSTEM_H
#define SCRIPTSYSTEM_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "../core/EngineGlobal.h"
#include <memory>
#include <vector>
#include <string>

namespace MQEngine {
    class ENGINE_API ScriptSystem {
    public:
        ScriptSystem();
        ~ScriptSystem();
        
        /**
         * @brief 加载并执行JavaScript脚本文件
         * @details 从./res/scripts/dist/和./res/scripts/js目录下获取所有js文件，
         *          然后使用NodeEnvironment执行所有的js文件
         */
        void loadScripts();
        
        /**
         * @brief 获取当前JavaScript环境中的所有函数名
         * @return JavaScript函数名列表
         */
        std::vector<std::string> getFunctionNames() const;
        
    private:
        DataManager* m_dataManager;
        std::unique_ptr<FCT::NodeEnvironment> m_nodeEnv;
        
        /**
         * @brief 从指定目录加载所有JavaScript文件
         * @param directory 目录路径
         * @return JavaScript文件路径列表
         */
        std::vector<std::string> loadJSFilesFromDirectory(const std::string& directory);
        
        /**
         * @brief 读取文件内容
         * @param filePath 文件路径
         * @return 文件内容字符串
         */
        std::string readFileContent(const std::string& filePath);
    };
} // MQEngine

#endif //SCRIPTSYSTEM_H
