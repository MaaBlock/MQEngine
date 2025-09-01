//
// Created by Administrator on 2025/8/31.
//

#ifndef SCRIPTSYSTEM_H
#define SCRIPTSYSTEM_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "../core/EngineGlobal.h"
#include "ComponentReflection.h"
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
        
        /**
         * @brief 更新系统，执行所有ScriptComponent的脚本
         */
        void update();
        
        /**
         * @brief 设置逻辑帧时间间隔
         * @param deltaTime 帧时间间隔（秒）
         */
        void setLogicDeltaTime(float deltaTime);
        
    private:
        DataManager* m_dataManager;
        std::unique_ptr<FCT::NodeEnvironment> m_nodeEnv;
        std::unique_ptr<ComponentReflection> m_componentReflection;
        float m_logicDeltaTime = 0.0f;
        
        /**
         * 从指定目录加载所有JavaScript文件
         * @param directory 目录路径
         * @return 文件路径列表
         */
        std::vector<std::string> loadJSFilesFromDirectory(const std::string& directory);

        /**
         * 注册实体成员函数到JavaScript环境
         */
        void registerEntityFunctions();
        
        /**
         * @brief 通用类型转换函数：从JSObject转换为ComponentValue
         * @param fieldType 字段类型
         * @param jsObject JavaScript对象
         * @param fieldName 字段名称（同时用于错误输出）
         * @return 转换后的ComponentValue
         */
        ComponentValue convertJSObjectToComponentValue(const std::string& fieldType, FCT::JSAny& jsAny, const std::string& fieldName);
        
        ComponentValue convertJSObjectToComponentValue(const std::string& fieldType, const FCT::JSAny& fieldValue);
        std::pair<entt::registry*, entt::entity> getEntityFromJS(FCT::NodeEnvironment& env);
        v8::Local<v8::Value> convertComponentValueToJS(const ComponentValue& value);
        
        /**
         * @brief 读取文件内容
         * @param filePath 文件路径
         * @return 文件内容字符串
         */
        std::string readFileContent(const std::string& filePath);
    };
} // MQEngine

#endif //SCRIPTSYSTEM_H
