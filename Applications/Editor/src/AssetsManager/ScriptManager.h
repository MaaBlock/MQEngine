#ifndef SCRIPTMANAGER_H
#define SCRIPTMANAGER_H
#include "../thirdparty/thirdparty.h"
#include "../core/Global.h"

namespace MQEngine {
    
    struct ScriptFunctionInfo {
        std::string functionName;
        std::string scriptPath;
        std::string description;
    };
    
    class ScriptManager {
    public:
        ScriptManager();
        ~ScriptManager();
        
        /**
         * @brief 渲染脚本管理器UI界面
         */
        void render();
        
        /**
         * @brief 刷新脚本列表和函数信息
         */
        void refreshScriptList();
        
        /**
         * @brief 获取所有可用的脚本函数
         * @return 脚本函数信息列表
         */
        const std::vector<ScriptFunctionInfo>& getScriptFunctions() const;
        
        /**
         * @brief 根据函数名获取脚本函数信息
         * @param functionName 函数名
         * @return 脚本函数信息，如果未找到返回空
         */
        std::optional<ScriptFunctionInfo> getScriptFunction(const std::string& functionName) const;
        
    private:
        
        /**
         * @brief 从ScriptSystem加载函数名
         */
        void loadFunctionNamesFromScriptSystem();
        

        
        /**
         * @brief 渲染脚本函数列表
         */
        void renderScriptFunctionList();

        /**
         * @brief 从ScriptSystem加载类名
         */
        void loadClassNamesFromScriptSystem();

        /**
         * @brief 渲染脚本类列表
         */
        void renderScriptClassList();

    private:
        std::vector<ScriptFunctionInfo> m_scriptFunctions;
        std::vector<std::string> m_scriptClasses; // 新增：存储类名
        std::string m_selectedFunction;
        std::string m_selectedClass; // 新增：选中的类名
        bool m_needRefresh = true;
        
        // UI状态
        bool m_isWindowHovered = false;
        bool m_isWindowFocused = false;
        char m_searchBuffer[256] = {0};
        std::vector<ScriptFunctionInfo> m_filteredFunctions;
    };
    
} // MQEngine

#endif //SCRIPTMANAGER_H