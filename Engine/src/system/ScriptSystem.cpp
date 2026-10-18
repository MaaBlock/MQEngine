#include "ScriptSystem.h"
#include "../core/engine.h"
#include "../system/InputSystem.h"
#include "../data/DataError.h"
#include "../data/DataLoader.h"
#include "../data/DataManager.h"
#include "../data/DiskResourceLoader.h"
#include "../data/ResourceLoader.h"
#include "../data/Scene.h"
#include "../data/SceneTrunk.h"
#include "../data/NameTag.h"
#include "../manager/ProjectManager.h"
#include "../manager/SystemManager.h"
#include "../plugin/PluginManager.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>

namespace MQEngine {

    using absl::FailedPreconditionError;

    namespace {

    std::string EscapeJsSingleQuotedString(const std::string& input) {
        std::string out;
        out.reserve(input.size() + 16);
        for (char c : input) {
            switch (c) {
                case '\\': out += "\\\\"; break;
                case '\'': out += "\\'"; break;
                case '\r': out += "\\r"; break;
                case '\n': out += "\\n"; break;
                default: out += c; break;
            }
        }
        return out;
    }

    std::string GetStringField(const boost::json::object& obj, const char* key, const std::string& defaultVal = "") {
        auto it = obj.find(key);
        if (it != obj.end() && it->value().is_string()) {
            return std::string(it->value().as_string().c_str());
        }
        return defaultVal;
    }

    uint32_t GetUintField(const boost::json::object& obj, const char* key, uint32_t defaultVal = 0) {
        auto it = obj.find(key);
        if (it == obj.end()) {
            return defaultVal;
        }
        if (it->value().is_uint64()) {
            return static_cast<uint32_t>(it->value().as_uint64());
        }
        if (it->value().is_int64()) {
            return static_cast<uint32_t>(it->value().as_int64());
        }
        if (it->value().is_double()) {
            return static_cast<uint32_t>(it->value().as_double());
        }
        return defaultVal;
    }

    bool GetBoolField(const boost::json::object& obj, const char* key, bool defaultVal = false) {
        auto it = obj.find(key);
        if (it != obj.end() && it->value().is_bool()) {
            return it->value().as_bool();
        }
        return defaultVal;
    }

    float JsonValueToFloat(const boost::json::value& val, float defaultVal = 0.0f) {
        if (val.is_double()) return static_cast<float>(val.as_double());
        if (val.is_int64()) return static_cast<float>(val.as_int64());
        if (val.is_uint64()) return static_cast<float>(val.as_uint64());
        return defaultVal;
    }

    } // namespace
    
    ScriptSystem::ScriptSystem() 
        : m_dataManager(g_engineGlobal.dataManager)
        , m_nodeEnv(std::make_unique<FCT::JS::NodeEnvironment>())
        , m_componentReflection(std::make_unique<ComponentReflection>())
    {
        m_nodeEnv->addModulePath("./Assets/scripts/node_modules");
        if (!m_nodeEnv->setup()) {
            std::cerr << "Failed to setup NodeEnvironment" << std::endl;
        } else {
            initializeCoreEngineBridge();
        }
    }
    
    ScriptSystem::~ScriptSystem() {
        if (m_nodeEnv) {
            m_nodeEnv->stop();
        }
    }
    
    void ScriptSystem::loadScripts() {
        ZoneScoped();
        if (m_isScriptsLoaded) return;

        if (!m_dataManager) {
            std::cerr << "DataManager not available." << std::endl;
            return;
        }

        if (!m_nodeEnv) {
            std::cerr << "NodeEnvironment not initialized." << std::endl;
            return;
        }

        try {
            initializeCoreEngineBridge();

            m_nodeEnv->excuteScript(R"(
                globalThis.entityInfo = { registry_ptr: 0, entity_id: 0 };
                globalThis.engine = {
                    logicDealt: 0.0
                };
                
                globalThis.Input = {
                    isKeyPressed: function(key) { return false; },
                    getMousePosition: function() { return {x: 0, y: 0}; }
                };

                globalThis.createEntityObject = function(entityId, registryPtr) {
                    let obj = {
                        entity: {
                            id: entityId,
                            registry: registryPtr
                        },
                        _tickers: {},
                        _onTicker: function() {
                            const tickers = this._tickers;
                            for (let key in tickers) {
                                if (typeof tickers[key] === 'function') {
                                    tickers[key].call(this);
                                }
                            }
                        }
                    };
                    obj.components = new Proxy({}, {
                        get: function(target, prop) {
                            return globalThis.entityInfo.getComponentById(obj.entity.registry, obj.entity.id, prop);
                        },
                        set: function(target, prop, value) {
                            return globalThis.entityInfo.setComponentById(obj.entity.registry, obj.entity.id, prop, value);
                        }
                    });
                    return obj;
                };
                globalThis.assignGlobalFunction = function(obj, name) {
                     if (typeof globalThis[name] === 'function') {
                         if (!obj._tickers) obj._tickers = {};
                         obj._tickers[name] = globalThis[name];
                     }
                };

                globalThis.createScriptClassInstance = function(className, entityId, registryPtr) {
                    const ClassConstructor = globalThis[className];
                    if (typeof ClassConstructor === 'function' && ClassConstructor.prototype.constructor === ClassConstructor) {
                        const instance = new ClassConstructor();
                        
                        console.log("Created instance of " + className);
                        console.log("Has onTicker?", typeof instance.onTicker);

                        instance.entity = {
                            id: entityId,
                            registry: registryPtr
                        };
                        instance.components = new Proxy({}, {
                            get: function(target, prop) {
                                return globalThis.entityInfo.getComponentById(instance.entity.registry, instance.entity.id, prop);
                            },
                            set: function(target, prop, value) {
                                return globalThis.entityInfo.setComponentById(instance.entity.registry, instance.entity.id, prop, value);
                            }
                        });

                        if (!instance._tickers) instance._tickers = {};
                        instance._onTicker = function() {
                            const tickers = this._tickers;
                            for (let key in tickers) {
                                if (typeof tickers[key] === 'function') {
                                    tickers[key].call(this);
                                }
                            }
                        };
                        
                        return instance;
                    } else {
                        console.error('Class not found or not a valid constructor:', className);
                        return null;
                    }
                };

                globalThis.getGlobalClassNames = function() {
                    const ignored = new Set(['globalThis', 'console', 'process', 'entityInfo', 'engine', 'MQEngine', '__mqPlugins', '__mqPluginRuntimeInitialized', '__mqStripTypeScriptFallback', '__mqNativeStripTS', '__mqEngineNativeCall', 'createEntityObject', 'assignGlobalFunction', 'createScriptClassInstance', 'getGlobalClassNames', 'getComponent', 'setComponent', 'hasComponent', 'addComponent', 'removeComponent']);
                    return Object.getOwnPropertyNames(globalThis).filter(prop => {
                        if (ignored.has(prop)) return false;
                        try {
                            const val = globalThis[prop];
                            if (typeof val !== 'function') return false;
                            const str = val.toString();
                            if (str.includes('[native code]')) return false;
                            return true;
                        } catch (e) { return false; }
                    });
                };
            )");

            registerEntityFunctions();
            registerInputFunctions();
            
            std::vector<std::string> distFiles = loadScriptFilesFromDirectory("./Assets/scripts/dist/", ".js");
            std::vector<std::string> jsFiles = loadScriptFilesFromDirectory("./Assets/scripts/js/", ".js");
            std::vector<std::string> tsInJsDir = loadScriptFilesFromDirectory("./Assets/scripts/js/", ".ts");
            std::vector<std::string> tsFiles = loadScriptFilesFromDirectory("./Assets/scripts/ts/", ".ts");

            std::vector<std::string> allScriptFiles;
            allScriptFiles.insert(allScriptFiles.end(), distFiles.begin(), distFiles.end());
            allScriptFiles.insert(allScriptFiles.end(), jsFiles.begin(), jsFiles.end());
            allScriptFiles.insert(allScriptFiles.end(), tsInJsDir.begin(), tsInJsDir.end());
            allScriptFiles.insert(allScriptFiles.end(), tsFiles.begin(), tsFiles.end());

            executeLoadedScriptFiles(allScriptFiles);
            
            std::cout << "Loaded and executed " << allScriptFiles.size() << " script files (.js/.ts)." << std::endl;
            m_isScriptsLoaded = true;
            
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

    void ScriptSystem::registerInputFunctions() {
        FCT::JS::Object globalObj = m_nodeEnv->global();
        FCT::JS::Object inputObj = globalObj["Input"];

        inputObj["isKeyPressed"] = [](int key) -> bool {
            if (g_engineGlobal.inputSystem) {
                return g_engineGlobal.inputSystem->isKeyPressed(key);
            }
            return false;
        };

        inputObj["getMousePosition"] = [this]() -> v8::Local<v8::Value> {
            if (g_engineGlobal.inputSystem) {
                FCT::Vec2 pos = g_engineGlobal.inputSystem->getMousePosition();
                v8::Local<v8::Object> posObj = v8::Object::New(m_nodeEnv->isolate());
                posObj->Set(m_nodeEnv->context(), convertToJS(*m_nodeEnv, "x").As<v8::String>(), convertToJS(*m_nodeEnv, pos.x));
                posObj->Set(m_nodeEnv->context(), convertToJS(*m_nodeEnv, "y").As<v8::String>(), convertToJS(*m_nodeEnv, pos.y));
                return posObj;
            }
            return v8::Undefined(m_nodeEnv->isolate());
        };
    }

    void ScriptSystem::executeLoadedScriptFiles(const std::vector<std::string>& scriptFiles) {
        for (const std::string& filePath : scriptFiles) {
            try {
                std::string rawCode = readFileContent(filePath);
                if (!rawCode.empty()) {
                    std::cout << "Executing script: " << filePath << std::endl;
                    std::string executableCode = filePath.ends_with(".ts")
                        ? transpileTypeScript(rawCode)
                        : rawCode;
                    m_nodeEnv->excuteScript(executableCode);
                    m_nodeEnv->tick();
                }
            } catch (const std::exception& e) {
                std::cerr << "Error executing script " << filePath << ": " << e.what() << std::endl;
            }
        }
    }

    void ScriptSystem::reloadScripts() {

    }
    
    std::vector<std::string> ScriptSystem::loadJSFilesFromDirectory(const std::string& directory) {
        return loadScriptFilesFromDirectory(directory, ".js");
    }

    std::vector<std::string> ScriptSystem::loadScriptFilesFromDirectory(const std::string& directory, const std::string& ext) {
        std::vector<std::string> scriptFiles;
        
        if (!m_dataManager) {
            return scriptFiles;
        }
        
        DataLoader* dataLoader = m_dataManager->getDataLoader();
        if (!dataLoader) {
            return scriptFiles;
        }
        
        try {
            dataLoader->ensureDirectory(directory);
            
            if (dataLoader->directoryExists(directory)) {
                scriptFiles = dataLoader->getFilePathsWithExtension(directory, ext);
            }
        } catch (const DataError& e) {
            std::cerr << "Error loading script files from " << directory << ": " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error loading script files from " << directory << ": " << e.what() << std::endl;
        }
        
        return scriptFiles;
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

    std::vector<std::string> ScriptSystem::getClassNames() const {
        if (!m_nodeEnv) {
            return {};
        }

        try {
            FCT::JS::Array resultArray = const_cast<FCT::JS::NodeEnvironment*>(m_nodeEnv.get())
                                        ->callFunction<FCT::JS::Array>("getGlobalClassNames");
            
            std::vector<std::string> names;
            uint32_t length = resultArray.length();
            names.reserve(length);

            for (uint32_t i = 0; i < length; ++i) {
                names.push_back(resultArray.get<std::string>(i));
            }
            return names;
        } catch (const std::exception& e) {
            std::cerr << "Error getting class names: " << e.what() << std::endl;
            return {};
        }
    }
    
    void ScriptSystem::setLogicDeltaTime(float deltaTime) {
        m_logicDeltaTime = deltaTime;
        
        if (m_nodeEnv) {
            try {
                auto global = m_nodeEnv->global();
                FCT::JS::Object engine = global["engine"];
                engine["logicDealt"] = deltaTime;
            } catch (const std::exception& e) {
                std::cerr << "Error updating logicDealt: " << e.what() << std::endl;
            }
        }
    }
    
    void ScriptSystem::update() {
        ZoneScoped();
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
            
            auto cacheView = registry->view<CacheScriptComponent>();
            for (auto entity : cacheView) {
                auto& cache = cacheView.get<CacheScriptComponent>(entity);
                if (cache.m_jsObject) {
                    try {
                        if (cache.m_jsObject->hasProperty("onTicker")) {
                            cache.m_jsObject->call("onTicker");
                        }
                        if (cache.m_jsObject->hasProperty("_onTicker")) {
                            cache.m_jsObject->call("_onTicker");
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "Error calling script ticker: " << e.what() << std::endl;
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
        ZoneScoped();
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
    
    ComponentValue ScriptSystem::convertJSObjectToComponentValue(const std::string& fieldType, FCT::JS::Any& jsAny, const std::string& fieldName) {
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
                FCT::JS::Object jsObject = jsAny.to<FCT::JS::Object>();
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

    ComponentValue ScriptSystem::convertJSObjectToComponentValue(const std::string& fieldType, const FCT::JS::Any& fieldValue) {
        // 由于Any的拷贝构造函数被删除，我们需要通过const_cast来移除const限定符
        FCT::JS::Any& mutableFieldValue = const_cast<FCT::JS::Any&>(fieldValue);
        return convertJSObjectToComponentValue(fieldType, mutableFieldValue, "unknown");
    }

    std::pair<entt::registry*, entt::entity> ScriptSystem::getEntityFromJS(FCT::JS::NodeEnvironment& env) {
        auto registries = m_dataManager->currentRegistries();
        if (registries.empty()) {
            return {nullptr, entt::null};
        }

        FCT::JS::Object globalObj = env.global();
        FCT::JS::Object entityInfo = globalObj["entityInfo"];
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
        
        FCT::JS::Object globalObj = env.global();
        FCT::JS::Object entityInfo = globalObj["entityInfo"];
        
        entityInfo["getComponent"] = [this](std::string componentName) -> v8::Local<v8::Value> {
            try {
                FCT::JS::Object globalObj = m_nodeEnv->global();
                FCT::JS::Object entityInfo = globalObj["entityInfo"];
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
        
        entityInfo["setComponent"] = [this](std::string componentName, FCT::JS::Object componentObj) -> bool {
            try {
                FCT::JS::Object globalObj = m_nodeEnv->global();
                FCT::JS::Object entityInfo = globalObj["entityInfo"];
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
                                FCT::JS::Any fieldValue = componentObj.get<FCT::JS::Any>(fieldName);
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
                FCT::JS::Object globalObj = m_nodeEnv->global();
                FCT::JS::Object entityInfo = globalObj["entityInfo"];
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
        
        entityInfo["setComponentField"] = [this](std::string componentName, std::string fieldName, FCT::JS::Any fieldValue) -> bool {
            try {
                FCT::JS::Object globalObj = m_nodeEnv->global();
                FCT::JS::Object entityInfo = globalObj["entityInfo"];
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
                FCT::JS::Object globalObj = m_nodeEnv->global();
                FCT::JS::Object entityInfo = globalObj["entityInfo"];
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
                FCT::JS::Object globalObj = m_nodeEnv->global();
                FCT::JS::Object entityInfo = globalObj["entityInfo"];
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
                FCT::JS::Object globalObj = m_nodeEnv->global();
                FCT::JS::Object entityInfo = globalObj["entityInfo"];
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

        entityInfo["getComponentById"] = [this](double registryPtrDouble, uint32_t entityId, std::string componentName) -> v8::Local<v8::Value> {
            try {
                uint64_t registryPtr = static_cast<uint64_t>(registryPtrDouble);
                auto registries = m_dataManager->currentRegistries();
                if (registries.empty()) return v8::Undefined(m_nodeEnv->isolate());

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    
                    if (!m_componentReflection->hasComponent(*targetRegistry, entity, componentName)) {
                        return v8::Undefined(m_nodeEnv->isolate());
                    }

                    v8::Local<v8::Object> jsObject = v8::Object::New(m_nodeEnv->isolate());
                    auto fieldNames = m_componentReflection->getComponentFieldNames(componentName);

                    for (const auto& fieldName : fieldNames) {
                        try {
                            auto value = m_componentReflection->getComponentField(*targetRegistry, entity, componentName, fieldName);
                            v8::Local<v8::Value> jsValue = convertComponentValueToJS(value);
                            jsObject->Set(m_nodeEnv->context(), convertToJS(*m_nodeEnv, fieldName).As<v8::String>(), jsValue);
                        } catch (const std::exception& e) {
                            // Suppress individual field errors or log them
                        }
                    }
                    return jsObject;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error in entityInfo.getComponentById: " << e.what() << std::endl;
            }
            return v8::Undefined(m_nodeEnv->isolate());
        };

        entityInfo["setComponentById"] = [this](double registryPtrDouble, uint32_t entityId, std::string componentName, FCT::JS::Object componentObj) -> bool {
            try {
                uint64_t registryPtr = static_cast<uint64_t>(registryPtrDouble);
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
                                FCT::JS::Any fieldValue = componentObj.get<FCT::JS::Any>(fieldName);
                                ComponentValue componentValue = convertJSObjectToComponentValue(fieldType, fieldValue, fieldName);
                                m_componentReflection->setComponentField(*targetRegistry, entity, componentName, fieldName, componentValue);
                            } catch (const std::exception& e) {
                                // Suppress error
                            }
                        }
                    }
                    return true;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error in entityInfo.setComponentById: " << e.what() << std::endl;
            }
            return false;
        };
    }

    std::string ScriptSystem::transpileTypeScript(const std::string& tsSource) {
        std::string code = tsSource;

        // 移除 import type 与 export type 声明
        code = std::regex_replace(code, std::regex(R"((^|\n)\s*import\s+type\s+[^\n;]+;?)"), "$1");
        code = std::regex_replace(code, std::regex(R"((^|\n)\s*export\s+type\s+[A-Za-z0-9_]+\s*=\s*[^;]+;)"), "$1");
        code = std::regex_replace(code, std::regex(R"((^|\n)\s*type\s+[A-Za-z0-9_]+\s*=\s*[^;]+;)"), "$1");

        // 移除顶层 interface 块（支持单层嵌套对象描述）
        code = std::regex_replace(
            code,
            std::regex(R"((^|\n)\s*(?:export\s+)?interface\s+[A-Za-z0-9_]+(?:\s*<[^>]*>)?(?:\s+extends\s+[^{]+)?\s*\{(?:[^{}]|\{[^{}]*\})*\})"),
            "$1");

        // 将 ES Module 默认导出转换为 CommonJS 导出
        code = std::regex_replace(code, std::regex(R"((^|\n)\s*export\s+default\s+)"), "$1module.exports = ");

        // 移除函数/方法返回类型注解 ): ReturnType { 或 ): ReturnType =>
        code = std::regex_replace(
            code,
            std::regex(R"(\)\s*:\s*[A-Za-z0-9_<>\[\]\.\|\&\s]+(\s*(?:\{|=>)))"),
            ")$1");

        // 移除变量声明类型注解 const/let/var x: Type =
        code = std::regex_replace(
            code,
            std::regex(R"(\b(const|let|var)\s+([A-Za-z_$][A-Za-z0-9_$]*)\s*:\s*[A-Za-z0-9_<>\[\]\.\|\&\s]+(\s*=))"),
            "$1 $2$3");

        // 移除参数列表中的常见标量/对象类型注解
        code = std::regex_replace(
            code,
            std::regex(R"(([\(\,]\s*[A-Za-z_$][A-Za-z0-9_$]*)\??\s*:\s*(?:string|number|boolean|any|unknown|void|never|object|Record<[^>]+>|Array<[^>]+>|[A-Za-z_$][A-Za-z0-9_$\.]*(?:\[\])?)(?=\s*[,\)\=]))"),
            "$1");

        // 移除 as Type 类型断言
        code = std::regex_replace(
            code,
            std::regex(R"(\s+as\s+(?:any|unknown|string|number|boolean|Record<[^>]+>|[A-Za-z_$][A-Za-z0-9_$\.]*(?:\[\])?)\b)"),
            "");

        return code;
    }

    void ScriptSystem::initializeCoreEngineBridge() {
        if (!m_nodeEnv || m_isCoreBridgeInitialized) {
            return;
        }

        FCT::JS::Object globalObj = m_nodeEnv->global();
        globalObj["__mqNativeStripTS"] = [](std::string tsCode) -> std::string {
            return ScriptSystem::transpileTypeScript(tsCode);
        };

        globalObj["__mqEngineNativeCall"] = [this](std::string action, std::string payloadJson) -> std::string {
            return this->dispatchEngineBridgeCall(action, payloadJson);
        };

        static const char* kCoreBridgeBootstrap = R"JS(
            if (!globalThis.__mqPluginRuntimeInitialized) {
                globalThis.__mqPluginRuntimeInitialized = true;
                globalThis.__mqPlugins = globalThis.__mqPlugins || {};

                const nodeModule = require('module');
                const nodeFs = require('fs');

                globalThis.__mqStripTypeScriptFallback = function(tsCode, filename) {
                    if (typeof nodeModule.stripTypeScriptTypes === 'function') {
                        try {
                            return nodeModule.stripTypeScriptTypes(tsCode, { mode: 'strip', sourceUrl: filename || 'script.ts' });
                        } catch (e) {}
                    }
                    try {
                        const ts = require('typescript');
                        if (ts && typeof ts.transpileModule === 'function') {
                            const out = ts.transpileModule(tsCode, {
                                compilerOptions: {
                                    module: ts.ModuleKind.CommonJS,
                                    target: ts.ScriptTarget.ES2022,
                                    esModuleInterop: true
                                }
                            });
                            return out.outputText;
                        }
                    } catch (e) {}
                    if (typeof globalThis.__mqNativeStripTS === 'function') {
                        return globalThis.__mqNativeStripTS(tsCode);
                    }
                    return tsCode;
                };

                if (nodeModule._extensions && !nodeModule._extensions['.ts']) {
                    nodeModule._extensions['.ts'] = function(mod, filename) {
                        const raw = nodeFs.readFileSync(filename, 'utf8');
                        const compiled = globalThis.__mqStripTypeScriptFallback(raw, filename);
                        mod._compile(compiled, filename);
                    };
                }

                globalThis.MQEngine = globalThis.MQEngine || {};
                globalThis.MQEngine.call = function(action, params) {
                    const payload = params ? JSON.stringify(params) : '{}';
                    const rawRes = globalThis.__mqEngineNativeCall(String(action), payload);
                    try {
                        return JSON.parse(rawRes);
                    } catch (e) {
                        return { ok: false, error: 'Invalid JSON response from native bridge', raw: rawRes };
                    }
                };

                globalThis.MQEngine.getEngineStatus = function() { return globalThis.MQEngine.call('getEngineStatus', {}); };
                globalThis.MQEngine.listPlugins = function() { return globalThis.MQEngine.call('listPlugins', {}); };
                globalThis.MQEngine.setPlayMode = function(running) { return globalThis.MQEngine.call('setPlayMode', { running: !!running }); };
                globalThis.MQEngine.listScenes = function() { return globalThis.MQEngine.call('listScenes', {}); };
                globalThis.MQEngine.openScene = function(uuid) { return globalThis.MQEngine.call('openScene', { uuid: String(uuid) }); };
                globalThis.MQEngine.saveScene = function(uuid) { return globalThis.MQEngine.call('saveScene', { uuid: uuid ? String(uuid) : '' }); };
                globalThis.MQEngine.newScene = function(name) { return globalThis.MQEngine.call('newScene', { name: String(name) }); };
                globalThis.MQEngine.listEntities = function(params) { return globalThis.MQEngine.call('listEntities', params || {}); };
                globalThis.MQEngine.getEntity = function(params) { return globalThis.MQEngine.call('getEntity', params || {}); };
                globalThis.MQEngine.createEntity = function(params) { return globalThis.MQEngine.call('createEntity', params || {}); };
                globalThis.MQEngine.destroyEntity = function(params) { return globalThis.MQEngine.call('destroyEntity', params || {}); };
                globalThis.MQEngine.listComponentTypes = function() { return globalThis.MQEngine.call('listComponentTypes', {}); };
                globalThis.MQEngine.getComponent = function(params) { return globalThis.MQEngine.call('getComponent', params || {}); };
                globalThis.MQEngine.setComponent = function(params) { return globalThis.MQEngine.call('setComponent', params || {}); };
                globalThis.MQEngine.addComponent = function(params) { return globalThis.MQEngine.call('addComponent', params || {}); };
                globalThis.MQEngine.removeComponent = function(params) { return globalThis.MQEngine.call('removeComponent', params || {}); };

                globalThis.MQEngine.registerPlugin = function(pluginDef) {
                    if (pluginDef && pluginDef.name) {
                        globalThis.__mqPlugins[pluginDef.name] = pluginDef;
                    }
                    return pluginDef;
                };
            }
        )JS";

        try {
            m_nodeEnv->excuteScript(kCoreBridgeBootstrap);
            m_nodeEnv->tick();
            m_isCoreBridgeInitialized = true;
        } catch (const std::exception& e) {
            spdlog::error("ScriptSystem 初始化核心 JS/TS 桥接失败: {}", e.what());
        }
    }

    Status ScriptSystem::executePluginScript(const std::string& pluginName,
                                             const std::string& pluginVersion,
                                             const std::string& entryPath,
                                             const std::string& pluginDir,
                                             ResourceLoader* resourceLoader) {
        if (!m_nodeEnv) {
            return FailedPreconditionError("ScriptSystem NodeEnvironment 未初始化");
        }

        initializeCoreEngineBridge();

        std::unique_ptr<DiskResourceLoader> fallbackLoader;
        if (!resourceLoader) {
            fallbackLoader = std::make_unique<DiskResourceLoader>();
            resourceLoader = fallbackLoader.get();
        }

        auto scriptRes = resourceLoader->loadFile(entryPath);
        CHECK_STATUS(scriptRes);

        const std::string& rawSource = scriptRes.value();
        bool isTypeScript = entryPath.ends_with(".ts");
        std::string precompiledSource = isTypeScript ? transpileTypeScript(rawSource) : rawSource;

        std::string base64Source = m_nodeEnv->base64Encode(precompiledSource);
        std::string escapedName = EscapeJsSingleQuotedString(pluginName);
        std::string escapedVersion = EscapeJsSingleQuotedString(pluginVersion);
        std::string escapedFile = EscapeJsSingleQuotedString(entryPath);
        std::string escapedDir = EscapeJsSingleQuotedString(pluginDir);

        std::string evalWrapper = StrCat(
            "(function() {\n"
            "  const nodeModule = require('module');\n"
            "  const __filename = '", escapedFile, "';\n"
            "  const __dirname = '", escapedDir, "';\n"
            "  const localRequire = nodeModule.createRequire(__filename);\n"
            "  const module = { exports: {} };\n"
            "  const exports = module.exports;\n"
            "  const sourceText = Buffer.from('", base64Source, "', 'base64').toString('utf8');\n"
            "  const compiledText = ", (isTypeScript ? "globalThis.__mqStripTypeScriptFallback(sourceText, __filename)" : "sourceText"), ";\n"
            "  const fn = new Function('exports', 'require', 'module', '__filename', '__dirname', 'MQEngine', compiledText);\n"
            "  const ret = fn(exports, localRequire, module, __filename, __dirname, globalThis.MQEngine);\n"
            "  let pluginInstance = module.exports;\n"
            "  if (pluginInstance && typeof pluginInstance.default === 'object') {\n"
            "    pluginInstance = pluginInstance.default;\n"
            "  }\n"
            "  if (typeof pluginInstance === 'function') {\n"
            "    pluginInstance = new pluginInstance();\n"
            "  }\n"
            "  if ((!pluginInstance || Object.keys(pluginInstance).length === 0) && ret && typeof ret === 'object') {\n"
            "    pluginInstance = ret;\n"
            "  }\n"
            "  if (!globalThis.__mqPlugins['", escapedName, "']) {\n"
            "    pluginInstance = pluginInstance || {};\n"
            "    pluginInstance.name = pluginInstance.name || '", escapedName, "';\n"
            "    pluginInstance.version = pluginInstance.version || '", escapedVersion, "';\n"
            "    globalThis.__mqPlugins['", escapedName, "'] = pluginInstance;\n"
            "  }\n"
            "})();");

        try {
            m_nodeEnv->excuteScript(evalWrapper);
            m_nodeEnv->tick();
        } catch (const std::exception& e) {
            return InternalError(StrCat("执行脚本插件 [", pluginName, "] 失败: ", e.what()));
        }
        return OkStatus();
    }

    Status ScriptSystem::invokePluginLifecycle(const std::string& pluginName,
                                               const std::string& methodName,
                                               const std::string& argsJson) {
        if (!m_nodeEnv) {
            return FailedPreconditionError("ScriptSystem NodeEnvironment 未初始化");
        }

        std::string escapedName = EscapeJsSingleQuotedString(pluginName);
        std::string escapedMethod = EscapeJsSingleQuotedString(methodName);
        std::string callScript = StrCat(
            "(function() {\n"
            "  const plugin = globalThis.__mqPlugins && globalThis.__mqPlugins['", escapedName, "'];\n"
            "  if (!plugin || typeof plugin['", escapedMethod, "'] !== 'function') return;\n"
            "  const argObj = ", argsJson, ";\n"
            "  if ('", escapedMethod, "' === 'onLogicTick') {\n"
            "    plugin.onLogicTick(argObj.dt || 0.016);\n"
            "  } else {\n"
            "    plugin['", escapedMethod, "'](globalThis.MQEngine);\n"
            "  }\n"
            "})();");

        try {
            m_nodeEnv->excuteScript(callScript);
        } catch (const std::exception& e) {
            return InternalError(StrCat("调用脚本插件 [", pluginName, "::", methodName, "] 异常: ", e.what()));
        }
        return OkStatus();
    }

    void ScriptSystem::unloadPluginScript(const std::string& pluginName) {
        if (!m_nodeEnv) {
            return;
        }
        (void)invokePluginLifecycle(pluginName, "onUnload");
        try {
            std::string escapedName = EscapeJsSingleQuotedString(pluginName);
            m_nodeEnv->excuteScript(
                "if (globalThis.__mqPlugins) { delete globalThis.__mqPlugins['" + escapedName + "']; }");
            m_nodeEnv->tick();
        } catch (...) {
        }
    }

    std::string ScriptSystem::dispatchEngineBridgeCall(const std::string& action, const std::string& payloadJson) {
        boost::json::object params;
        if (!payloadJson.empty()) {
            boost::system::error_code ec;
            auto parsed = boost::json::parse(payloadJson, ec);
            if (!ec && parsed.is_object()) {
                params = parsed.as_object();
            }
        }

        boost::json::object response;
        try {
            if (action == "getEngineStatus") {
                response = handleGetEngineStatus();
            } else if (action == "listPlugins") {
                response = handleListPlugins();
            } else if (action == "setPlayMode") {
                response = handleSetPlayMode(params);
            } else if (action == "listScenes") {
                response = handleListScenes();
            } else if (action == "openScene") {
                response = handleOpenScene(params);
            } else if (action == "saveScene") {
                response = handleSaveScene(params);
            } else if (action == "newScene") {
                response = handleNewScene(params);
            } else if (action == "listEntities") {
                response = handleListEntities(params);
            } else if (action == "getEntity") {
                response = handleGetEntity(params);
            } else if (action == "createEntity") {
                response = handleCreateEntity(params);
            } else if (action == "destroyEntity") {
                response = handleDestroyEntity(params);
            } else if (action == "listComponentTypes") {
                response = handleListComponentTypes();
            } else if (action == "getComponent") {
                response = handleGetComponent(params);
            } else if (action == "setComponent") {
                response = handleSetComponent(params);
            } else if (action == "addComponent") {
                response = handleAddComponent(params);
            } else if (action == "removeComponent") {
                response = handleRemoveComponent(params);
            } else {
                response["ok"] = false;
                response["error"] = StrCat("未知的引擎桥接操作: ", action);
            }
        } catch (const std::exception& e) {
            response["ok"] = false;
            response["error"] = e.what();
        }

        return boost::json::serialize(response);
    }

    boost::json::object ScriptSystem::handleGetEngineStatus() {
        boost::json::object res;
        res["ok"] = true;
        res["isRunning"] = g_engineGlobal.isRunning;

        Engine* engine = Engine::s_instance;
        if (engine && engine->getApplication()) {
            res["windowTitle"] = engine->getApplication()->renderConfig().windowTitle;
        }

        if (engine && engine->getProjectManager()) {
            auto* projMgr = engine->getProjectManager();
            boost::json::object projObj;
            projObj["isOpen"] = projMgr->isProjectOpen();
            projObj["projectName"] = projMgr->getCurrentConfig().projectName;
            projObj["projectFilePath"] = projMgr->getCurrentConfig().projectFilePath;
            projObj["projectDirectory"] = projMgr->getCurrentConfig().projectDirectory;
            projObj["assetRoot"] = projMgr->getCurrentConfig().resolvedAssetRoot;
            res["project"] = projObj;
        }

        DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
        if (dm) {
            boost::json::object sceneObj;
            std::string curUuid = dm->getCurrentSceneUuid();
            sceneObj["currentSceneUuid"] = curUuid;
            sceneObj["currentSceneName"] = curUuid.empty() ? "" : dm->getSceneNameByUuid(curUuid);
            sceneObj["assetRoot"] = dm->getAssetRoot();
            res["scene"] = sceneObj;
        }

        return res;
    }

    boost::json::object ScriptSystem::handleListPlugins() {
        boost::json::object res;
        res["ok"] = true;
        boost::json::array arr;

        Engine* engine = Engine::s_instance;
        PluginManager* pm = engine ? engine->getPluginManager() : nullptr;
        if (pm) {
            for (const auto& name : pm->getAllPluginNames()) {
                const PluginEntry* entry = pm->getPluginEntry(name);
                boost::json::object item;
                item["name"] = name;
                if (entry) {
                    item["version"] = entry->plugin ? entry->plugin->getVersion() : "";
                    item["state"] = static_cast<int>(entry->state);
                    item["isDynamic"] = entry->isDynamic;
                    item["isScript"] = entry->isScript;
                    item["path"] = entry->dynamicLibraryPath;
                }
                arr.push_back(item);
            }
        }

        res["plugins"] = arr;
        return res;
    }

    boost::json::object ScriptSystem::handleSetPlayMode(const boost::json::object& params) {
        bool running = GetBoolField(params, "running", false);
        g_engineGlobal.isRunning = running;
        if (g_engineGlobal.systemManager) {
            g_engineGlobal.systemManager->requestSetSystemEnabled("ScriptCacheSystem", running);
        }

        boost::json::object res;
        res["ok"] = true;
        res["isRunning"] = g_engineGlobal.isRunning;
        return res;
    }

    boost::json::object ScriptSystem::handleListScenes() {
        boost::json::object res;
        DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
        if (!dm) {
            res["ok"] = false;
            res["error"] = "DataManager 未就绪";
            return res;
        }

        boost::json::array scenesArr;
        const auto& uuidMap = dm->getSceneUuidToNameMap();
        std::string currentUuid = dm->getCurrentSceneUuid();
        for (const auto& [uuid, name] : uuidMap) {
            boost::json::object item;
            item["uuid"] = uuid;
            item["name"] = name;
            item["isActive"] = (uuid == currentUuid);
            scenesArr.push_back(item);
        }

        res["ok"] = true;
        res["currentSceneUuid"] = currentUuid;
        res["scenes"] = scenesArr;
        return res;
    }

    boost::json::object ScriptSystem::handleOpenScene(const boost::json::object& params) {
        boost::json::object res;
        DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
        if (!dm) {
            res["ok"] = false;
            res["error"] = "DataManager 未就绪";
            return res;
        }

        std::string uuid = GetStringField(params, "uuid");
        if (uuid.empty()) {
            res["ok"] = false;
            res["error"] = "缺少场景 uuid 参数";
            return res;
        }

        dm->openScene(uuid);
        res["ok"] = true;
        res["currentSceneUuid"] = dm->getCurrentSceneUuid();
        res["currentSceneName"] = dm->getSceneNameByUuid(uuid);
        return res;
    }

    boost::json::object ScriptSystem::handleSaveScene(const boost::json::object& params) {
        boost::json::object res;
        DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
        if (!dm) {
            res["ok"] = false;
            res["error"] = "DataManager 未就绪";
            return res;
        }

        std::string uuid = GetStringField(params, "uuid");
        if (uuid.empty()) {
            uuid = dm->getCurrentSceneUuid();
        }
        if (uuid.empty()) {
            res["ok"] = false;
            res["error"] = "当前无打开的场景";
            return res;
        }

        bool ok = dm->saveScene(uuid);
        res["ok"] = ok;
        res["savedSceneUuid"] = uuid;
        return res;
    }

    boost::json::object ScriptSystem::handleNewScene(const boost::json::object& params) {
        boost::json::object res;
        DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
        if (!dm) {
            res["ok"] = false;
            res["error"] = "DataManager 未就绪";
            return res;
        }

        std::string name = GetStringField(params, "name");
        if (name.empty()) {
            res["ok"] = false;
            res["error"] = "缺少场景名称参数 name";
            return res;
        }

        dm->newScene(name);
        res["ok"] = true;
        res["sceneName"] = name;
        return res;
    }

    entt::registry* ScriptSystem::resolveTargetRegistry(const std::string& trunkName, std::string& outResolvedTrunk) const {
        DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
        if (!dm) {
            return nullptr;
        }

        Scene* scene = dm->getCurrentScene();
        if (scene) {
            std::string targetTrunk = trunkName.empty() ? "default" : trunkName;
            SceneTrunk* trunk = scene->getLoadedTrunk(targetTrunk);
            if (trunk && trunk->getRegistry()) {
                outResolvedTrunk = targetTrunk;
                return trunk->getRegistry();
            }
            for (const auto& tName : scene->getTrunkList()) {
                SceneTrunk* candidate = scene->getLoadedTrunk(tName);
                if (candidate && candidate->getRegistry()) {
                    outResolvedTrunk = tName;
                    return candidate->getRegistry();
                }
            }
            if (scene->getRegistry()) {
                outResolvedTrunk = "scene";
                return scene->getRegistry();
            }
        }

        auto registries = dm->currentRegistries();
        if (!registries.empty()) {
            outResolvedTrunk = "global";
            return registries.front();
        }
        return nullptr;
    }

    boost::json::object ScriptSystem::handleListEntities(const boost::json::object& params) {
        boost::json::object res;
        DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
        ComponentReflection* refl = m_componentReflection.get();

        if (!dm || !refl) {
            res["ok"] = false;
            res["error"] = "DataManager 或 ComponentReflection 未就绪";
            return res;
        }

        std::string filterTrunk = GetStringField(params, "trunk");
        boost::json::array entitiesArr;

        auto collectFromRegistry = [&](entt::registry* reg, const std::string& trunkLabel) {
            if (!reg) return;
            const auto& entityStorage = reg->storage<entt::entity>();

            for (auto entity : entityStorage) {
                if (!reg->valid(entity)) continue;

                boost::json::object entityObj;
                entityObj["entityId"] = static_cast<uint32_t>(entt::to_integral(entity));
                entityObj["trunk"] = trunkLabel;

                std::string entityName;
                if (reg->all_of<NameTag>(entity)) {
                    entityName = reg->get<NameTag>(entity).name;
                }
                entityObj["name"] = entityName;

                boost::json::array compNames;
                for (const auto& compName : refl->getRegisteredComponentNames()) {
                    if (refl->hasComponent(*reg, entity, compName)) {
                        compNames.push_back(boost::json::value(compName));
                    }
                }
                entityObj["components"] = compNames;
                entitiesArr.push_back(entityObj);
            }
        };

        Scene* scene = dm->getCurrentScene();
        if (scene) {
            for (const auto& trunkName : scene->getTrunkList()) {
                if (!filterTrunk.empty() && filterTrunk != trunkName) continue;
                SceneTrunk* trunk = scene->getLoadedTrunk(trunkName);
                if (trunk && trunk->getRegistry()) {
                    collectFromRegistry(trunk->getRegistry(), trunkName);
                }
            }
        } else {
            auto registries = dm->currentRegistries();
            for (size_t i = 0; i < registries.size(); ++i) {
                collectFromRegistry(registries[i], "registry_" + std::to_string(i));
            }
        }

        res["ok"] = true;
        res["count"] = entitiesArr.size();
        res["entities"] = entitiesArr;
        return res;
    }

    boost::json::object ScriptSystem::handleGetEntity(const boost::json::object& params) {
        boost::json::object res;
        std::string resolvedTrunk;
        entt::registry* reg = resolveTargetRegistry(GetStringField(params, "trunk"), resolvedTrunk);
        if (!reg) {
            res["ok"] = false;
            res["error"] = "未找到可用的场景实体注册表";
            return res;
        }

        uint32_t entityId = GetUintField(params, "entityId");
        entt::entity entity = static_cast<entt::entity>(entityId);
        if (!reg->valid(entity)) {
            res["ok"] = false;
            res["error"] = StrCat("无效的实体 ID: ", entityId);
            return res;
        }

        res["ok"] = true;
        res["entityId"] = entityId;
        res["trunk"] = resolvedTrunk;
        res["name"] = reg->all_of<NameTag>(entity) ? reg->get<NameTag>(entity).name : "";
        res["components"] = serializeEntityComponents(*reg, entity);
        return res;
    }

    boost::json::object ScriptSystem::handleCreateEntity(const boost::json::object& params) {
        boost::json::object res;
        std::string resolvedTrunk;
        entt::registry* reg = resolveTargetRegistry(GetStringField(params, "trunk"), resolvedTrunk);
        if (!reg) {
            res["ok"] = false;
            res["error"] = "未找到可用的场景实体注册表";
            return res;
        }

        entt::entity entity = reg->create();
        std::string name = GetStringField(params, "name", "NewEntity");
        reg->emplace_or_replace<NameTag>(entity, NameTag{ name });

        if (params.contains("components") && params.at("components").is_object() && m_componentReflection) {
            for (const auto& [compName, compVal] : params.at("components").as_object()) {
                std::string cName(compName);
                m_componentReflection->addComponent(*reg, entity, cName);
                if (compVal.is_object()) {
                    (void)applyComponentFieldsFromJson(*reg, entity, cName, compVal.as_object());
                }
            }
        }

        uint32_t entityId = static_cast<uint32_t>(entt::to_integral(entity));
        res["ok"] = true;
        res["entityId"] = entityId;
        res["name"] = name;
        res["trunk"] = resolvedTrunk;
        res["components"] = serializeEntityComponents(*reg, entity);
        return res;
    }

    boost::json::object ScriptSystem::handleDestroyEntity(const boost::json::object& params) {
        boost::json::object res;
        std::string resolvedTrunk;
        entt::registry* reg = resolveTargetRegistry(GetStringField(params, "trunk"), resolvedTrunk);
        if (!reg) {
            res["ok"] = false;
            res["error"] = "未找到可用的场景实体注册表";
            return res;
        }

        uint32_t entityId = GetUintField(params, "entityId");
        entt::entity entity = static_cast<entt::entity>(entityId);
        if (!reg->valid(entity)) {
            res["ok"] = false;
            res["error"] = StrCat("实体不存在或已销毁: ", entityId);
            return res;
        }

        reg->destroy(entity);
        res["ok"] = true;
        res["destroyedEntityId"] = entityId;
        return res;
    }

    boost::json::object ScriptSystem::handleListComponentTypes() {
        boost::json::object res;
        if (!m_componentReflection) {
            res["ok"] = false;
            res["error"] = "ComponentReflection 未就绪";
            return res;
        }

        boost::json::array typesArr;
        for (const auto& [compName, compInfo] : m_componentReflection->getComponentInfos()) {
            boost::json::object compObj;
            compObj["name"] = compName;
            boost::json::array fieldsArr;
            for (const auto& field : compInfo.fields) {
                boost::json::object fObj;
                fObj["name"] = field.name;
                fObj["type"] = field.type;
                fieldsArr.push_back(fObj);
            }
            compObj["fields"] = fieldsArr;
            typesArr.push_back(compObj);
        }

        res["ok"] = true;
        res["componentTypes"] = typesArr;
        return res;
    }

    boost::json::object ScriptSystem::handleGetComponent(const boost::json::object& params) {
        boost::json::object res;
        std::string resolvedTrunk;
        entt::registry* reg = resolveTargetRegistry(GetStringField(params, "trunk"), resolvedTrunk);
        if (!reg || !m_componentReflection) {
            res["ok"] = false;
            res["error"] = "Registry 或 ComponentReflection 未就绪";
            return res;
        }

        uint32_t entityId = GetUintField(params, "entityId");
        std::string compName = GetStringField(params, "componentName");
        entt::entity entity = static_cast<entt::entity>(entityId);
        if (!reg->valid(entity)) {
            res["ok"] = false;
            res["error"] = StrCat("无效的实体 ID: ", entityId);
            return res;
        }
        if (!m_componentReflection->hasComponent(*reg, entity, compName)) {
            res["ok"] = false;
            res["error"] = StrCat("实体 [", entityId, "] 不包含组件: ", compName);
            return res;
        }

        res["ok"] = true;
        res["entityId"] = entityId;
        res["componentName"] = compName;
        res["fields"] = serializeComponentFields(*reg, entity, compName);
        return res;
    }

    boost::json::object ScriptSystem::handleSetComponent(const boost::json::object& params) {
        boost::json::object res;
        std::string resolvedTrunk;
        entt::registry* reg = resolveTargetRegistry(GetStringField(params, "trunk"), resolvedTrunk);
        if (!reg || !m_componentReflection) {
            res["ok"] = false;
            res["error"] = "Registry 或 ComponentReflection 未就绪";
            return res;
        }

        uint32_t entityId = GetUintField(params, "entityId");
        std::string compName = GetStringField(params, "componentName");
        entt::entity entity = static_cast<entt::entity>(entityId);
        if (!reg->valid(entity)) {
            res["ok"] = false;
            res["error"] = StrCat("无效的实体 ID: ", entityId);
            return res;
        }

        if (!m_componentReflection->hasComponent(*reg, entity, compName)) {
            m_componentReflection->addComponent(*reg, entity, compName);
        }

        if (params.contains("fields") && params.at("fields").is_object()) {
            Status st = applyComponentFieldsFromJson(*reg, entity, compName, params.at("fields").as_object());
            if (!st.ok()) {
                res["ok"] = false;
                res["error"] = std::string(st.message());
                return res;
            }
        }

        res["ok"] = true;
        res["entityId"] = entityId;
        res["componentName"] = compName;
        res["fields"] = serializeComponentFields(*reg, entity, compName);
        return res;
    }

    boost::json::object ScriptSystem::handleAddComponent(const boost::json::object& params) {
        return handleSetComponent(params);
    }

    boost::json::object ScriptSystem::handleRemoveComponent(const boost::json::object& params) {
        boost::json::object res;
        std::string resolvedTrunk;
        entt::registry* reg = resolveTargetRegistry(GetStringField(params, "trunk"), resolvedTrunk);
        if (!reg || !m_componentReflection) {
            res["ok"] = false;
            res["error"] = "Registry 或 ComponentReflection 未就绪";
            return res;
        }

        uint32_t entityId = GetUintField(params, "entityId");
        std::string compName = GetStringField(params, "componentName");
        entt::entity entity = static_cast<entt::entity>(entityId);
        if (!reg->valid(entity)) {
            res["ok"] = false;
            res["error"] = StrCat("无效的实体 ID: ", entityId);
            return res;
        }

        m_componentReflection->removeComponent(*reg, entity, compName);
        res["ok"] = true;
        res["entityId"] = entityId;
        res["removedComponent"] = compName;
        return res;
    }

    boost::json::object ScriptSystem::serializeEntityComponents(entt::registry& registry, entt::entity entity) const {
        boost::json::object comps;
        if (!m_componentReflection) {
            return comps;
        }

        for (const auto& compName : m_componentReflection->getRegisteredComponentNames()) {
            if (m_componentReflection->hasComponent(registry, entity, compName)) {
                comps[compName] = serializeComponentFields(registry, entity, compName);
            }
        }
        return comps;
    }

    boost::json::object ScriptSystem::serializeComponentFields(entt::registry& registry,
                                                               entt::entity entity,
                                                               const std::string& componentName) const {
        boost::json::object fields;
        if (!m_componentReflection) {
            return fields;
        }

        for (const auto& fieldName : m_componentReflection->getComponentFieldNames(componentName)) {
            try {
                ComponentValue val = m_componentReflection->getComponentField(registry, entity, componentName, fieldName);
                fields[fieldName] = componentValueToJson(val);
            } catch (...) {
            }
        }
        return fields;
    }

    Status ScriptSystem::applyComponentFieldsFromJson(entt::registry& registry,
                                                      entt::entity entity,
                                                      const std::string& componentName,
                                                      const boost::json::object& fieldsObj) {
        if (!m_componentReflection) {
            return FailedPreconditionError("ComponentReflection 未就绪");
        }

        for (const auto& [fieldKey, fieldVal] : fieldsObj) {
            std::string fieldName(fieldKey);
            std::string fieldType = m_componentReflection->getComponentFieldType(componentName, fieldName);
            if (fieldType.empty()) {
                continue;
            }
            auto cvRes = jsonToComponentValue(fieldType, fieldVal);
            CHECK_STATUS(cvRes);
            m_componentReflection->setComponentField(registry, entity, componentName, fieldName, cvRes.value());
        }
        return OkStatus();
    }

    boost::json::value ScriptSystem::componentValueToJson(const ComponentValue& val) {
        return std::visit([](const auto& v) -> boost::json::value {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool> ||
                          std::is_same_v<T, int> ||
                          std::is_same_v<T, float> ||
                          std::is_same_v<T, double>) {
                return boost::json::value(v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return boost::json::value(v);
            } else if constexpr (std::is_same_v<T, Vec3>) {
                boost::json::object vecObj;
                vecObj["x"] = v.x;
                vecObj["y"] = v.y;
                vecObj["z"] = v.z;
                return vecObj;
            } else {
                return nullptr;
            }
        }, val);
    }

    StatusOr<ComponentValue> ScriptSystem::jsonToComponentValue(const std::string& fieldType,
                                                                const boost::json::value& jv) {
        if (fieldType == "bool") {
            if (jv.is_bool()) return ComponentValue(jv.as_bool());
            return InvalidArgumentError("字段类型要求 bool");
        }
        if (fieldType == "int") {
            if (jv.is_int64()) return ComponentValue(static_cast<int>(jv.as_int64()));
            if (jv.is_uint64()) return ComponentValue(static_cast<int>(jv.as_uint64()));
            if (jv.is_double()) return ComponentValue(static_cast<int>(jv.as_double()));
            return InvalidArgumentError("字段类型要求 int");
        }
        if (fieldType == "float") {
            return ComponentValue(JsonValueToFloat(jv));
        }
        if (fieldType == "double") {
            if (jv.is_double()) return ComponentValue(jv.as_double());
            if (jv.is_int64()) return ComponentValue(static_cast<double>(jv.as_int64()));
            if (jv.is_uint64()) return ComponentValue(static_cast<double>(jv.as_uint64()));
            return InvalidArgumentError("字段类型要求 double");
        }
        if (fieldType == "string" || fieldType == "std::string") {
            if (jv.is_string()) return ComponentValue(std::string(jv.as_string().c_str()));
            return InvalidArgumentError("字段类型要求 string");
        }
        if (fieldType == "Vec3") {
            if (jv.is_object()) {
                const auto& obj = jv.as_object();
                float x = obj.contains("x") ? JsonValueToFloat(obj.at("x")) : 0.0f;
                float y = obj.contains("y") ? JsonValueToFloat(obj.at("y")) : 0.0f;
                float z = obj.contains("z") ? JsonValueToFloat(obj.at("z")) : 0.0f;
                return ComponentValue(Vec3(x, y, z));
            }
            if (jv.is_array() && jv.as_array().size() >= 3) {
                const auto& arr = jv.as_array();
                return ComponentValue(Vec3(
                    JsonValueToFloat(arr[0]),
                    JsonValueToFloat(arr[1]),
                    JsonValueToFloat(arr[2])));
            }
            return InvalidArgumentError("字段类型要求 Vec3 ({x,y,z} 或 [x,y,z])");
        }
        return InvalidArgumentError(StrCat("不支持的字段类型: ", fieldType));
    }
} // namespace MQEngine