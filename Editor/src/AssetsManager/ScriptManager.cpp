#include "ScriptManager.h"
#include "../thirdparty/thirdparty.h"
#include <filesystem>
#include <algorithm>

#define TEXT(str) (const char*)u8##str
using namespace FCT;

namespace MQEngine {
    
    ScriptManager::ScriptManager() {
    }
    
    ScriptManager::~ScriptManager() {

    }
    
    void ScriptManager::render() {
        if (m_needRefresh || (g_engineGlobal.scriptSystem && m_scriptFunctions.empty())) {
            refreshScriptList();
        }
        
        ImGui::Begin(TEXT("脚本管理器"), nullptr, ImGuiWindowFlags_MenuBar);

        m_isWindowHovered = ImGui::IsWindowHovered();
        m_isWindowFocused = ImGui::IsWindowFocused();

        if (ImGui::BeginMenuBar()) {
            if (ImGui::MenuItem(TEXT("刷新脚本"))) {
                refreshScriptList();
            }
            ImGui::EndMenuBar();
        }

        ImGui::Text(TEXT("搜索函数:"));
        ImGui::SameLine();
        if (ImGui::InputText("##ScriptSearch", m_searchBuffer, sizeof(m_searchBuffer))) {
            m_filteredFunctions.clear();
            std::string searchTerm = m_searchBuffer;
            std::transform(searchTerm.begin(), searchTerm.end(), searchTerm.begin(), ::tolower);
            
            for (const auto& func : m_scriptFunctions) {
                std::string funcName = func.functionName;
                std::transform(funcName.begin(), funcName.end(), funcName.begin(), ::tolower);
                
                if (searchTerm.empty() || funcName.find(searchTerm) != std::string::npos) {
                    m_filteredFunctions.push_back(func);
                }
            }
        }
        
        ImGui::Separator();

        ImGui::Text(TEXT("可用函数数量: %zu"), m_scriptFunctions.size());
        
        if (strlen(m_searchBuffer) > 0) {
            ImGui::Text(TEXT("过滤后函数数量: %zu"), m_filteredFunctions.size());
        }
        
        ImGui::Separator();

        renderScriptFunctionList();

        
        ImGui::End();
    }
    
    void ScriptManager::refreshScriptList() {
        m_scriptFunctions.clear();
        m_filteredFunctions.clear();


        loadFunctionNamesFromScriptSystem();

        m_filteredFunctions = m_scriptFunctions;
        
        m_needRefresh = false;
    }
    
    const std::vector<ScriptFunctionInfo>& ScriptManager::getScriptFunctions() const {
        return m_scriptFunctions;
    }
    
    std::optional<ScriptFunctionInfo> ScriptManager::getScriptFunction(const std::string& functionName) const {
        auto it = std::find_if(m_scriptFunctions.begin(), m_scriptFunctions.end(),
            [&functionName](const ScriptFunctionInfo& info) {
                return info.functionName == functionName;
            });
        
        if (it != m_scriptFunctions.end()) {
            return *it;
        }
        
        return std::nullopt;
    }
    
    void ScriptManager::loadFunctionNamesFromScriptSystem() {
        m_scriptFunctions.clear();

        if (g_engineGlobal.scriptSystem) {
            try {
                g_engineGlobal.scriptSystem->loadScripts();
                
                auto functionNames = g_engineGlobal.scriptSystem->getFunctionNames();
                
                for (const auto& funcName : functionNames) {
                    ScriptFunctionInfo info;
                    info.functionName = funcName;
                    info.scriptPath = "JavaScript Global";
                    info.description = TEXT("JavaScript函数: ") + funcName;
                    m_scriptFunctions.push_back(info);
                }
                
                fout << TEXT("从ScriptSystem加载了 ") << m_scriptFunctions.size() << TEXT(" 个脚本函数") << std::endl;
            } catch (const std::exception& e) {
                fout << TEXT("从ScriptSystem加载函数名时出错: ") << e.what() << std::endl;
            }
        } else {
            fout << TEXT("ScriptSystem未初始化，无法加载脚本函数") << std::endl;
        }
    }
    

    
    void ScriptManager::renderScriptFunctionList() {
        ImGui::BeginChild("ScriptFunctionList", ImVec2(0, -60), true);
        
        const auto& functionsToShow = strlen(m_searchBuffer) > 0 ? m_filteredFunctions : m_scriptFunctions;
        
        for (size_t i = 0; i < functionsToShow.size(); i++) {
            const auto& func = functionsToShow[i];

            bool isSelected = (m_selectedFunction == func.functionName);
            if (ImGui::Selectable(func.functionName.c_str(), isSelected)) {
                m_selectedFunction = func.functionName;
            }

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("SCRIPT_FUNCTION", func.functionName.c_str(), func.functionName.size() + 1);

                ImGui::Text(TEXT("拖拽脚本函数: %s"), func.functionName.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text(TEXT("函数名: %s"), func.functionName.c_str());
                ImGui::Text(TEXT("脚本路径: %s"), func.scriptPath.c_str());
                ImGui::Text(TEXT("描述: %s"), func.description.c_str());
                ImGui::EndTooltip();
            }
        }
        
        ImGui::EndChild();
    }

} // MQEngine