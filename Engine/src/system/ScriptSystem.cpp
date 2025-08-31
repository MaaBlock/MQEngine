#include "ScriptSystem.h"
#include "../data/DataError.h"
#include "../data/DataLoader.h"
#include "../data/DataManager.h"
#include <iostream>
#include <fstream>
#include <sstream>

namespace MQEngine {
    
    ScriptSystem::ScriptSystem() 
        : m_dataManager(g_engineGlobal.dataManager)
        , m_nodeEnv(std::make_unique<FCT::NodeEnvironment>())
    {
        if (!m_nodeEnv->setup()) {
            std::cerr << "Failed to setup NodeEnvironment" << std::endl;
        }
    }
    
    ScriptSystem::~ScriptSystem() {
        if (m_nodeEnv) {
            m_nodeEnv->stop();
        }
    }
    
    void ScriptSystem::loadScripts() {
        if (!m_dataManager) {
            std::cerr << "DataManager not available." << std::endl;
            return;
        }

        if (!m_nodeEnv) {
            std::cerr << "NodeEnvironment not initialized." << std::endl;
            return;
        }

        try {
            std::vector<std::string> distFiles = loadJSFilesFromDirectory("./res/scripts/dist/");
            std::vector<std::string> jsFiles = loadJSFilesFromDirectory("./res/scripts/js/");

            std::vector<std::string> allJSFiles;
            allJSFiles.insert(allJSFiles.end(), distFiles.begin(), distFiles.end());
            allJSFiles.insert(allJSFiles.end(), jsFiles.begin(), jsFiles.end());

            for (const std::string& filePath : allJSFiles) {
                try {
                    std::string jsCode = readFileContent(filePath);
                    if (!jsCode.empty()) {
                        std::cout << "Executing script: " << filePath << std::endl;
                        m_nodeEnv->excuteScript(jsCode);
                        m_nodeEnv->tick();
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error executing script " << filePath << ": " << e.what() << std::endl;
                }
            }
            
            std::cout << "Loaded and executed " << allJSFiles.size() << " JavaScript files." << std::endl;
            
        } catch (const DataError& e) {
            std::cerr << "DataError in loadScripts: " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error in loadScripts: " << e.what() << std::endl;
        }
        auto ref = m_nodeEnv->global().getFunctionNames();
        for (const std::string& name : ref) {
            std::cout << "Function Name: " << name << std::endl;
        }
    }
    
    std::vector<std::string> ScriptSystem::loadJSFilesFromDirectory(const std::string& directory) {
        std::vector<std::string> jsFiles;
        
        if (!m_dataManager) {
            return jsFiles;
        }
        
        DataLoader* dataLoader = m_dataManager->getDataLoader();
        if (!dataLoader) {
            return jsFiles;
        }
        
        try {
            dataLoader->ensureDirectory(directory);
            
            if (dataLoader->directoryExists(directory)) {
                jsFiles = dataLoader->getFilePathsWithExtension(directory, ".js");
            }
        } catch (const DataError& e) {
            std::cerr << "Error loading JS files from " << directory << ": " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error loading JS files from " << directory << ": " << e.what() << std::endl;
        }
        
        return jsFiles;
    }
    
    std::string ScriptSystem::readFileContent(const std::string& filePath) {
        if (!m_dataManager) {
            return "";
        }
        
        DataLoader* dataLoader = m_dataManager->getDataLoader();
        if (!dataLoader) {
            return "";
        }
        
        try {
            if (dataLoader->fileExists(filePath)) {
                auto inputStream = dataLoader->openBinaryInputStream(filePath);
                if (inputStream && inputStream->is_open()) {
                    std::stringstream buffer;
                    buffer << inputStream->rdbuf();
                    return buffer.str();
                }
            }
        } catch (const DataError& e) {
            std::cerr << "Error reading file " << filePath << ": " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error reading file " << filePath << ": " << e.what() << std::endl;
        }
        
        return "";
    }
    
} // namespace MQEngine