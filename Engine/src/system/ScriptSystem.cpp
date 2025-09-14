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
        , m_componentReflection(std::make_unique<ComponentReflection>())
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
            m_nodeEnv->excuteScript(R"(
                globalThis.entityInfo = { registry_ptr: 0, entity_id: 0 };
                globalThis.engine = {
                    logicDealt: 0.0
                };
            )");

            registerEntityFunctions();

            
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
    
    std::vector<std::string> ScriptSystem::getFunctionNames() const {
        if (!m_nodeEnv) {
            return {};
        }
        
        try {
            return m_nodeEnv->global().getFunctionNames();
        } catch (const std::exception& e) {
            std::cerr << "Error getting function names: " << e.what() << std::endl;
            return {};
        }
    }
    
    void ScriptSystem::setLogicDeltaTime(float deltaTime) {
        m_logicDeltaTime = deltaTime;
        
        if (m_nodeEnv) {
            try {
                auto global = m_nodeEnv->global();
                FCT::JSObject engine = global["engine"];
                engine["logicDealt"] = deltaTime;
            } catch (const std::exception& e) {
                std::cerr << "Error updating logicDealt: " << e.what() << std::endl;
            }
        }
    }
    
    void ScriptSystem::update() {
        if (!m_nodeEnv) {
            std::cerr << "NodeEnvironment not initialized." << std::endl;
            return;
        }
        
        if (!m_dataManager) {
            std::cerr << "DataManager not available." << std::endl;
            return;
        }

        auto registries = m_dataManager->currentRegistries();

        for (size_t registryIndex = 0; registryIndex < registries.size(); ++registryIndex) {
            auto& registry = registries[registryIndex];

            auto view = registry->view<TickerScriptComponent>();

            for (auto entity : view) {
                auto& scriptComponent = view.get<TickerScriptComponent>(entity);
                if (!scriptComponent.functionName.empty()) {
                    try {
                        uint64_t registryPtr = reinterpret_cast<uint64_t>(registry);
                        std::string setupCode = "globalThis.entityInfo.registry_ptr = " + std::to_string(registryPtr) + 
                                              "; globalThis.entityInfo.entity_id = " + std::to_string(static_cast<uint32_t>(entity)) + ";";
                        m_nodeEnv->excuteScript(setupCode);
                        
                        std::string callCode = scriptComponent.functionName + "();";
                        m_nodeEnv->excuteScript(callCode);
                    } catch (const std::exception& e) {
                        std::cerr << "Error executing script function '" << scriptComponent.functionName << "': " << e.what() << std::endl;
                    }
                }
            }
        }

        try {
            m_nodeEnv->tick();
        } catch (const std::exception& e) {
            std::cerr << "Error during NodeEnvironment tick: " << e.what() << std::endl;
        }
    }

    void ScriptSystem::start() {
        if (!m_nodeEnv || !m_dataManager) {
            return;
        }

        auto registries = m_dataManager->currentRegistries();
        for (auto& registry : registries) {
            auto view = registry->view<OnStartScriptComponent>();
            for (auto entity : view) {
                if (m_startedEntities.find(entity) == m_startedEntities.end()) {
                    const auto& scriptComponent = view.get<OnStartScriptComponent>(entity);
                    if (!scriptComponent.functionName.empty()) {
                        try {
                            uint64_t registryPtr = reinterpret_cast<uint64_t>(registry);
                            std::string setupCode = "globalThis.entityInfo.registry_ptr = " + std::to_string(registryPtr) +
                                                  "; globalThis.entityInfo.entity_id = " + std::to_string(static_cast<uint32_t>(entity)) + ";";
                            m_nodeEnv->excuteScript(setupCode);

                            std::string callCode = scriptComponent.functionName + "();";
                            m_nodeEnv->excuteScript(callCode);
                            m_startedEntities.insert(entity);
                        } catch (const std::exception& e) {
                            std::cerr << "Error executing OnStart script '" << scriptComponent.functionName << "': " << e.what() << std::endl;
                        }
                    }
                }
            }
        }
    }

    void ScriptSystem::cleanUp()
    {
        m_startedEntities.clear();
    }
    
    ComponentValue ScriptSystem::convertJSObjectToComponentValue(const std::string& fieldType, FCT::JSAny& jsAny, const std::string& fieldName) {
        ComponentValue componentValue;
        
        try {
            if (fieldType == "int") {
                int intValue = jsAny.to<int>();
                componentValue = intValue;
            } else if (fieldType == "float") {
                float floatValue = jsAny.to<float>();
                componentValue = floatValue;
            } else if (fieldType == "double") {
                double doubleValue = jsAny.to<double>();
                componentValue = doubleValue;
            } else if (fieldType == "bool") {
                bool boolValue = jsAny.to<bool>();
                componentValue = boolValue;
            } else if (fieldType == "std::string" || fieldType == "string") {
                std::string stringValue = jsAny.to<std::string>();
                componentValue = stringValue;
            } else if (fieldType == "Vec3") {
                // 处理Vec3类型
                FCT::JSObject jsObject = jsAny.to<FCT::JSObject>();
                if (jsObject.hasProperty("x") && jsObject.hasProperty("y") && jsObject.hasProperty("z")) {
                    float x = jsObject.get<float>("x");
                    float y = jsObject.get<float>("y");
                    float z = jsObject.get<float>("z");
                    
                    componentValue = FCT::Vec3(x, y, z);
                } else {
                    throw std::runtime_error("Vec3 object missing x, y, or z properties");
                }
            } else {
                throw std::runtime_error("Unsupported field type: " + fieldType);
            }
        } catch (const std::exception& e) {
            std::cerr << "Type conversion failed for field " << fieldName << " (expected type: " << fieldType << "): " << e.what() << std::endl;
            throw;
        }
        
        return componentValue;
    }

    ComponentValue ScriptSystem::convertJSObjectToComponentValue(const std::string& fieldType, const FCT::JSAny& fieldValue) {
        // 由于JSAny的拷贝构造函数被删除，我们需要通过const_cast来移除const限定符
        FCT::JSAny& mutableFieldValue = const_cast<FCT::JSAny&>(fieldValue);
        return convertJSObjectToComponentValue(fieldType, mutableFieldValue, "unknown");
    }

    std::pair<entt::registry*, entt::entity> ScriptSystem::getEntityFromJS(FCT::NodeEnvironment& env) {
        auto registries = m_dataManager->currentRegistries();
        if (registries.empty()) {
            return {nullptr, entt::null};
        }

        FCT::JSObject globalObj = env.global();
        FCT::JSObject entityInfo = globalObj["entityInfo"];
        uint64_t registryPtr = entityInfo["registry_ptr"];
        uint32_t entityId = entityInfo["entity_id"];

        entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
        auto it = std::find(registries.begin(), registries.end(), targetRegistry);
        if (it != registries.end()) {
            entt::entity entity = static_cast<entt::entity>(entityId);
            return {targetRegistry, entity};
        }
        
        return {nullptr, entt::null};
    }

    v8::Local<v8::Value> ScriptSystem::convertComponentValueToJS(const ComponentValue& value) {
        return std::visit([this](const auto& v) -> v8::Local<v8::Value> {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) {
                return convertToJS(*m_nodeEnv, v);
            } else if constexpr (std::is_same_v<T, int>) {
                return convertToJS(*m_nodeEnv, v);
            } else if constexpr (std::is_same_v<T, float>) {
                return convertToJS(*m_nodeEnv, v);
            } else if constexpr (std::is_same_v<T, double>) {
                return convertToJS(*m_nodeEnv, v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return convertToJS(*m_nodeEnv, v);
            } else if constexpr (std::is_same_v<T, FCT::Vec3>) {
                v8::Local<v8::Object> vec3Obj = v8::Object::New(m_nodeEnv->isolate());
                vec3Obj->Set(m_nodeEnv->context(), convertToJS(*m_nodeEnv, "x").As<v8::String>(), convertToJS(*m_nodeEnv, v.x));
                vec3Obj->Set(m_nodeEnv->context(), convertToJS(*m_nodeEnv, "y").As<v8::String>(), convertToJS(*m_nodeEnv, v.y));
                vec3Obj->Set(m_nodeEnv->context(), convertToJS(*m_nodeEnv, "z").As<v8::String>(), convertToJS(*m_nodeEnv, v.z));
                return vec3Obj;
            } else {
                return v8::Undefined(m_nodeEnv->isolate());
            }
        }, value);
    }

    void ScriptSystem::registerEntityFunctions() {
        auto& env = *m_nodeEnv;
        
        FCT::JSObject globalObj = env.global();
        FCT::JSObject entityInfo = globalObj["entityInfo"];
        
        entityInfo["getComponent"] = [this](std::string componentName) -> v8::Local<v8::Value> {
            try {
                FCT::JSObject globalObj = m_nodeEnv->global();
                FCT::JSObject entityInfo = globalObj["entityInfo"];
                uint64_t registryPtr = entityInfo["registry_ptr"];
                uint32_t entityId = entityInfo["entity_id"];
                
                auto registries = m_dataManager->currentRegistries();
                if (registries.empty()) return v8::Object::New(m_nodeEnv->isolate());

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    
                    if (!m_componentReflection->hasComponent(*targetRegistry, entity, componentName)) {
                        return v8::Object::New(m_nodeEnv->isolate());
                    }

                    v8::Local<v8::Object> jsObject = v8::Object::New(m_nodeEnv->isolate());
                    auto fieldNames = m_componentReflection->getComponentFieldNames(componentName);

                    for (const auto& fieldName : fieldNames) {
                        try {
                            auto value = m_componentReflection->getComponentField(*targetRegistry, entity, componentName, fieldName);
                            v8::Local<v8::Value> jsValue = convertComponentValueToJS(value);
                            jsObject->Set(m_nodeEnv->context(), convertToJS(*m_nodeEnv, fieldName).As<v8::String>(), jsValue);
                        } catch (const std::exception& e) {
                            std::cerr << "Error getting field " << fieldName << ": " << e.what() << std::endl;
                        }
                    }
                    
                    return jsObject;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error in entityInfo.getComponent: " << e.what() << std::endl;
            }
            return v8::Object::New(m_nodeEnv->isolate());
        };
        
        entityInfo["setComponent"] = [this](std::string componentName, FCT::JSObject componentObj) -> bool {
            try {
                FCT::JSObject globalObj = m_nodeEnv->global();
                FCT::JSObject entityInfo = globalObj["entityInfo"];
                uint64_t registryPtr = entityInfo["registry_ptr"];
                uint32_t entityId = entityInfo["entity_id"];
                
                auto registries = m_dataManager->currentRegistries();
                if (registries.empty()) return false;

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);

                    auto fieldNames = m_componentReflection->getComponentFieldNames(componentName);

                    for (const auto& fieldName : fieldNames) {
                        if (componentObj.hasProperty(fieldName)) {
                            std::string fieldType = m_componentReflection->getComponentFieldType(componentName, fieldName);
                            
                            try {
                                FCT::JSAny fieldValue = componentObj.get<FCT::JSAny>(fieldName);
                                ComponentValue componentValue = convertJSObjectToComponentValue(fieldType, fieldValue, fieldName);
                                m_componentReflection->setComponentField(*targetRegistry, entity, componentName, fieldName, componentValue);
                            } catch (const std::exception& e) {
                                std::cerr << "Failed to set field " << fieldName << ": " << e.what() << std::endl;
                            }
                        }
                    }
                    return true;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error in entityInfo.setComponent: " << e.what() << std::endl;
            }
            return false;
        };
        
        entityInfo["getComponentField"] = [this](std::string componentName, std::string fieldName) -> v8::Local<v8::Value> {
            try {
                FCT::JSObject globalObj = m_nodeEnv->global();
                FCT::JSObject entityInfo = globalObj["entityInfo"];
                uint64_t registryPtr = entityInfo["registry_ptr"];
                uint32_t entityId = entityInfo["entity_id"];
                
                auto registries = m_dataManager->currentRegistries();
                if (registries.empty()) return v8::Undefined(m_nodeEnv->isolate());

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    auto value = m_componentReflection->getComponentField(*targetRegistry, entity, componentName, fieldName);
                    return convertComponentValueToJS(value);
                }
            } catch (const std::exception& e) {
                std::cerr << "Error in entityInfo.getComponentField: " << e.what() << std::endl;
            }
            return v8::Undefined(m_nodeEnv->isolate());
        };
        
        entityInfo["setComponentField"] = [this](std::string componentName, std::string fieldName, FCT::JSAny fieldValue) -> bool {
            try {
                FCT::JSObject globalObj = m_nodeEnv->global();
                FCT::JSObject entityInfo = globalObj["entityInfo"];
                uint64_t registryPtr = entityInfo["registry_ptr"];
                uint32_t entityId = entityInfo["entity_id"];
                
                auto registries = m_dataManager->currentRegistries();
                if (registries.empty()) return false;

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    
                    std::string fieldType = m_componentReflection->getComponentFieldType(componentName, fieldName);
                    
                    try {
                        ComponentValue componentValue = convertJSObjectToComponentValue(fieldType, fieldValue, fieldName);
                        m_componentReflection->setComponentField(*targetRegistry, entity, componentName, fieldName, componentValue);
                        return true;
                    } catch (const std::exception& e) {
                        std::cerr << "Failed to set component field: " << e.what() << std::endl;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Error in entityInfo.setComponentField: " << e.what() << std::endl;
            }
            return false;
        };
        
        entityInfo["hasComponent"] = [this](std::string componentName) -> bool {
            try {
                FCT::JSObject globalObj = m_nodeEnv->global();
                FCT::JSObject entityInfo = globalObj["entityInfo"];
                uint64_t registryPtr = entityInfo["registry_ptr"];
                uint32_t entityId = entityInfo["entity_id"];
                
                auto registries = m_dataManager->currentRegistries();
                if (registries.empty()) return false;

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    return m_componentReflection->hasComponent(*targetRegistry, entity, componentName);
                }
            } catch (const std::exception& e) {
                std::cerr << "Error in entityInfo.hasComponent: " << e.what() << std::endl;
            }
            return false;
        };
        
        entityInfo["addComponent"] = [this](std::string componentName) -> bool {
            try {
                FCT::JSObject globalObj = m_nodeEnv->global();
                FCT::JSObject entityInfo = globalObj["entityInfo"];
                uint64_t registryPtr = entityInfo["registry_ptr"];
                uint32_t entityId = entityInfo["entity_id"];
                
                auto registries = m_dataManager->currentRegistries();
                if (registries.empty()) return false;

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    m_componentReflection->addComponent(*targetRegistry, entity, componentName);
                    return true;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error in entityInfo.addComponent: " << e.what() << std::endl;
            }
            return false;
        };
        
        entityInfo["removeComponent"] = [this](std::string componentName) -> bool {
            try {
                FCT::JSObject globalObj = m_nodeEnv->global();
                FCT::JSObject entityInfo = globalObj["entityInfo"];
                uint64_t registryPtr = entityInfo["registry_ptr"];
                uint32_t entityId = entityInfo["entity_id"];
                
                auto registries = m_dataManager->currentRegistries();
                if (registries.empty()) return false;

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    m_componentReflection->removeComponent(*targetRegistry, entity, componentName);
                    return true;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error in entityInfo.removeComponent: " << e.what() << std::endl;
            }
            return false;
        };
    }
} // namespace MQEngine