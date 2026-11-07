#include "NodeJsScriptBackend.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../../runtime/core/Runtime.h"
#include "../../runtime/core/Engine.h"
#include "../../scene/data/Component.h"
#include "../../resource/data/DataError.h"
#include "../../resource/core/DataLoader.h"
#include "../../scene/manager/DataManager.h"
#include "../../resource/core/DiskResourceLoader.h"
#include "../../resource/core/ResourceLoader.h"
#include "../../base/Foundation.h"
#include "../../base/MemoryManager.h"
#include "../../base/JobSystem.h"
#include "../../network/manager/NetworkManager.h"
#include "../../network/system/NetworkSyncSystem.h"
#include "../../animation/system/SkeletalAnimationSystem.h"
#include "../../hid/system/InputSystem.h"
#include "../../scene/system/ComponentReflection.h"
#include "../../physics/system/PhysicsSystem.h"
#include "../../hid/data/KeyCode.h"
#include "Engine_reflection.gen.h"
#include <iostream>
#include <regex>
#include <sstream>

namespace MQEngine {

    using absl::FailedPreconditionError;
    using Runtime::Engine;
    using EngineRuntime = Runtime::Runtime;

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

    } // namespace

    NodeJsScriptBackend::NodeJsScriptBackend(DataManager* dataManager)
        : m_dataManager(dataManager)
        , m_fallbackReflection(makeUnique<ComponentReflection>())
        , m_nodeEnv(makeUnique<FCT::JS::NodeEnvironment>())
        , m_enginePackage(makeUnique<FCT::NB::Package>("mqengine")) {
    }

    ComponentReflection* NodeJsScriptBackend::getActiveReflection() const {
        if (Engine::getInstance() && Engine::getInstance()->foundation && Engine::getInstance()->foundation->getComponentReflection()) {
            return Engine::getInstance()->foundation->getComponentReflection();
        }
        return m_fallbackReflection.get();
    }

    NodeJsScriptBackend::~NodeJsScriptBackend() {
        shutdown();
    }

    Status NodeJsScriptBackend::initialize() {
        if (!m_nodeEnv) {
            return FailedPreconditionError("NodeEnvironment 创建失败");
        }
        m_nodeEnv->addModulePath("./Assets/Scripts/node_modules");
        if (!m_nodeEnv->setup()) {
            return InternalError("NodeEnvironment 初始化失败");
        }
        initializeCoreBridge("./Assets/Scripts");
        injectEntityRuntimeBootstrap();
        registerEntityFunctions();
        registerInputFunctions();
        return OkStatus();
    }

    void NodeJsScriptBackend::shutdown() {
        m_enginePackage.reset();
        if (m_nodeEnv) {
            m_nodeEnv->stop();
            m_nodeEnv.reset();
        }
    }

    Status NodeJsScriptBackend::loadScriptsFromRoot(const std::string& scriptsRoot) {
        if (!m_nodeEnv) {
            return FailedPreconditionError("NodeEnvironment 未初始化");
        }

        try {
            m_nodeEnv->addModulePath(scriptsRoot + "/node_modules");
            initializeCoreBridge(scriptsRoot);
            exportTypeDefinitions(scriptsRoot);
            injectEntityRuntimeBootstrap();
            registerEntityFunctions();
            registerInputFunctions();

            std::vector<std::string> distFiles = loadScriptFilesFromDirectory(scriptsRoot + "/dist/", ".js");
            std::vector<std::string> jsFiles = loadScriptFilesFromDirectory(scriptsRoot + "/js/", ".js");
            std::vector<std::string> tsInJsDir = loadScriptFilesFromDirectory(scriptsRoot + "/js/", ".ts");
            std::vector<std::string> tsFiles = loadScriptFilesFromDirectory(scriptsRoot + "/ts/", ".ts");

            std::vector<std::string> allScriptFiles;
            allScriptFiles.insert(allScriptFiles.end(), distFiles.begin(), distFiles.end());
            allScriptFiles.insert(allScriptFiles.end(), jsFiles.begin(), jsFiles.end());
            allScriptFiles.insert(allScriptFiles.end(), tsInJsDir.begin(), tsInJsDir.end());
            allScriptFiles.insert(allScriptFiles.end(), tsFiles.begin(), tsFiles.end());

            executeLoadedScriptFiles(allScriptFiles);
            spdlog::info("NodeJsScriptBackend 已从 [{}] 加载并执行 {} 个脚本文件 (.js/.ts)", scriptsRoot, allScriptFiles.size());
        } catch (const DataError& e) {
            return InternalError(StrCat("加载脚本数据错误: ", e.what()));
        } catch (const std::exception& e) {
            return InternalError(StrCat("加载脚本异常: ", e.what()));
        }

        return OkStatus();
    }

    Status NodeJsScriptBackend::reloadScriptsFromRoot(const std::string& scriptsRoot) {
        return loadScriptsFromRoot(scriptsRoot);
    }

    void NodeJsScriptBackend::injectEntityRuntimeBootstrap() {
        if (!m_nodeEnv) {
            return;
        }
        m_nodeEnv->excuteScript(R"(
            globalThis.entityInfo = globalThis.entityInfo || { registry_ptr: 0, entity_id: 0 };
            globalThis.engine = globalThis.engine || {};
            globalThis.engine.logicDealt = globalThis.engine.logicDealt || 0.0;
            globalThis.__mqScriptClasses = globalThis.__mqScriptClasses || {};
            if (globalThis.MQEngine && globalThis.MQEngine.Engine) {
                Object.assign(globalThis.engine, globalThis.MQEngine.Engine);
            }
            
            globalThis.Input = globalThis.Input || {
                isKeyPressed: function(key) { return false; },
                isMouseButtonPressed: function(btn) { return false; },
                isLeftMousePressed: function() { return false; },
                isRightMousePressed: function() { return false; },
                getMousePosition: function() { return {x: 0, y: 0}; },
                getMouseDelta: function() { return {x: 0, y: 0}; },
                getMouseWheelDelta: function() { return 0; }
            };

            globalThis.__mqResolveComponentName = function(compOrName) {
                if (typeof compOrName === 'string') return compOrName;
                if (compOrName && (typeof compOrName === 'function' || typeof compOrName === 'object')) {
                    if (typeof compOrName.componentName === 'string' && compOrName.componentName.length > 0) {
                        return compOrName.componentName;
                    }
                    if (typeof compOrName.name === 'string' && compOrName.name.length > 0) {
                        return compOrName.name;
                    }
                }
                return String(compOrName || '');
            };

            globalThis.__mqCreateReactiveVecProxy = function(getEntityFn, compName, fieldName, rawVec) {
                const vecTarget = Object.assign({}, rawVec);
                return new Proxy(vecTarget, {
                    get: function(target, prop, receiver) {
                        return Reflect.get(target, prop, receiver);
                    },
                    set: function(target, prop, value) {
                        target[prop] = value;
                        const ent = getEntityFn();
                        if (ent && ent.registry) {
                            globalThis.entityInfo.setComponentFieldById(ent.registry, ent.id, compName, fieldName, target);
                        }
                        return true;
                    }
                });
            };

            globalThis.__mqCreateReactiveComponentProxy = function(getEntityFn, compName, autoAdd, initialSnapshot) {
                const baseTarget = (initialSnapshot && typeof initialSnapshot === 'object')
                    ? Object.assign({}, initialSnapshot)
                    : {};
                return new Proxy(baseTarget, {
                    get: function(target, prop, receiver) {
                        if (prop === '__isMqReactiveProxy') return true;
                        if (prop === '__componentName') return compName;
                        if (typeof prop === 'symbol' || prop === 'toJSON') {
                            return Reflect.get(target, prop, receiver);
                        }
                        const ent = getEntityFn();
                        if (!ent || !ent.registry) {
                            return target[prop];
                        }
                        if (autoAdd && !globalThis.entityInfo.hasComponentById(ent.registry, ent.id, compName)) {
                            globalThis.entityInfo.addComponentById(ent.registry, ent.id, compName);
                        }
                        const val = globalThis.entityInfo.getComponentFieldById(ent.registry, ent.id, compName, String(prop));
                        if (val === undefined) {
                            return target[prop];
                        }
                        target[prop] = val;
                        if (val && typeof val === 'object' && !Array.isArray(val)) {
                            return globalThis.__mqCreateReactiveVecProxy(getEntityFn, compName, String(prop), val);
                        }
                        return val;
                    },
                    set: function(target, prop, value) {
                        target[prop] = value;
                        const ent = getEntityFn();
                        if (ent && ent.registry) {
                            if (autoAdd && !globalThis.entityInfo.hasComponentById(ent.registry, ent.id, compName)) {
                                globalThis.entityInfo.addComponentById(ent.registry, ent.id, compName);
                            }
                            globalThis.entityInfo.setComponentFieldById(ent.registry, ent.id, compName, String(prop), value);
                        }
                        return true;
                    }
                });
            };

            globalThis.__mqCreateComponentsMapProxy = function(ownerObj) {
                return new Proxy({}, {
                    get: function(target, prop) {
                        if (typeof prop !== 'string') return undefined;
                        return ownerObj.getComponent(prop);
                    },
                    set: function(target, prop, value) {
                        if (typeof prop !== 'string') return false;
                        return ownerObj.setComponent(prop, value);
                    }
                });
            };

            {
                const __prevScriptBehaviourProto = (globalThis.ScriptBehaviour && globalThis.ScriptBehaviour.prototype) || null;
                class ScriptBehaviour {
                constructor() {
                    const pending = globalThis.__mqPendingEntityContext;
                    this.entity = pending
                        ? { id: pending.id, registry: pending.registry }
                        : { id: 0, registry: 0 };
                    this.components = globalThis.__mqCreateComponentsMapProxy(this);
                }
                get deltaTime() {
                    return (globalThis.engine && globalThis.engine.logicDealt) || 0.016;
                }
                get position() {
                    const comp = this.getComponent('PositionComponent');
                    return comp ? comp.position : undefined;
                }
                set position(val) {
                    if (val && this.entity && this.entity.registry) {
                        globalThis.entityInfo.setComponentFieldById(this.entity.registry, this.entity.id, 'PositionComponent', 'position', val);
                    }
                }
                get rotation() {
                    const comp = this.getComponent('RotationComponent');
                    return comp ? comp.rotation : undefined;
                }
                set rotation(val) {
                    if (val && this.entity && this.entity.registry) {
                        globalThis.entityInfo.setComponentFieldById(this.entity.registry, this.entity.id, 'RotationComponent', 'rotation', val);
                    }
                }
                get scale() {
                    const comp = this.getComponent('ScaleComponent');
                    return comp ? comp.scale : undefined;
                }
                set scale(val) {
                    if (val && this.entity && this.entity.registry) {
                        globalThis.entityInfo.setComponentFieldById(this.entity.registry, this.entity.id, 'ScaleComponent', 'scale', val);
                    }
                }
                getComponent(compOrName) {
                    const compName = globalThis.__mqResolveComponentName(compOrName);
                    if (!compName || !this.entity || !this.entity.registry) return undefined;
                    const rawObj = globalThis.entityInfo.getComponentById(this.entity.registry, this.entity.id, compName);
                    if (!rawObj) return undefined;
                    return globalThis.__mqCreateReactiveComponentProxy(() => this.entity, compName, false, rawObj);
                }
                setComponent(compOrName, value) {
                    const compName = globalThis.__mqResolveComponentName(compOrName);
                    if (!compName || !this.entity || !this.entity.registry) return false;
                    return globalThis.entityInfo.setComponentById(this.entity.registry, this.entity.id, compName, value);
                }
                hasComponent(compOrName) {
                    const compName = globalThis.__mqResolveComponentName(compOrName);
                    if (!compName || !this.entity || !this.entity.registry) return false;
                    return globalThis.entityInfo.hasComponentById(this.entity.registry, this.entity.id, compName);
                }
                addComponent(compOrName, initValue) {
                    const compName = globalThis.__mqResolveComponentName(compOrName);
                    if (!compName || !this.entity || !this.entity.registry) return undefined;
                    globalThis.entityInfo.addComponentById(this.entity.registry, this.entity.id, compName);
                    if (initValue && typeof initValue === 'object') {
                        globalThis.entityInfo.setComponentById(this.entity.registry, this.entity.id, compName, initValue);
                    }
                    return this.getComponent(compName);
                }
                removeComponent(compOrName) {
                    const compName = globalThis.__mqResolveComponentName(compOrName);
                    if (!compName || !this.entity || !this.entity.registry) return false;
                    return globalThis.entityInfo.removeComponentById(this.entity.registry, this.entity.id, compName);
                }
                useComponent(compOrName, autoAdd) {
                    const compName = globalThis.__mqResolveComponentName(compOrName);
                    return globalThis.__mqCreateReactiveComponentProxy(() => this.entity, compName, !!autoAdd, {});
                }
                setParent(parentEntityId, trunkName = 'default') {
                    if (!globalThis.MQEngine || !globalThis.MQEngine.DataManager || !this.entity) return false;
                    return globalThis.MQEngine.DataManager.setParent(trunkName, this.entity.id, parentEntityId);
                }
                clearParent(trunkName = 'default') {
                    if (!globalThis.MQEngine || !globalThis.MQEngine.DataManager || !this.entity) return false;
                    return globalThis.MQEngine.DataManager.clearParent(trunkName, this.entity.id);
                }
                getParent(trunkName = 'default') {
                    if (!globalThis.MQEngine || !globalThis.MQEngine.DataManager || !this.entity) return 0xFFFFFFFF;
                    return globalThis.MQEngine.DataManager.getParent(trunkName, this.entity.id);
                }
                getChildren(trunkName = 'default') {
                    if (!globalThis.MQEngine || !globalThis.MQEngine.DataManager || !this.entity) return [];
                    return globalThis.MQEngine.DataManager.getChildren(trunkName, this.entity.id);
                }
                getWorldPosition(trunkName = 'default') {
                    if (!globalThis.MQEngine || !globalThis.MQEngine.DataManager || !this.entity) return this.position || { x: 0, y: 0, z: 0 };
                    return globalThis.MQEngine.DataManager.getWorldPosition(trunkName, this.entity.id);
                }
                setWorldPosition(worldPos, trunkName = 'default') {
                    if (!globalThis.MQEngine || !globalThis.MQEngine.DataManager || !this.entity || !worldPos) return false;
                    return globalThis.MQEngine.DataManager.setWorldPosition(trunkName, this.entity.id, worldPos);
                }
                getPositionEngine(trunkName = 'default') {
                    return this.getWorldPosition(trunkName);
                }
                setPositionEngine(enginePos, trunkName = 'default') {
                    return this.setWorldPosition(enginePos, trunkName);
                }
                lookAt(target, trunkName = 'default') {
                    if (!globalThis.MQEngine || !globalThis.MQEngine.DataManager || !this.entity || !target) return false;
                    return globalThis.MQEngine.DataManager.lookAt(trunkName, this.entity.id, target);
                }
            }
            if (__prevScriptBehaviourProto) {
                for (const key of Object.getOwnPropertyNames(__prevScriptBehaviourProto)) {
                    if (key === 'constructor' || (key in ScriptBehaviour.prototype)) continue;
                    const desc = Object.getOwnPropertyDescriptor(__prevScriptBehaviourProto, key);
                    if (desc) {
                        Object.defineProperty(ScriptBehaviour.prototype, key, desc);
                    }
                }
            }
            if (globalThis.__mqScriptBehaviourExtensions && typeof globalThis.__mqScriptBehaviourExtensions === 'object') {
                for (const key of Object.getOwnPropertyNames(globalThis.__mqScriptBehaviourExtensions)) {
                    const desc = Object.getOwnPropertyDescriptor(globalThis.__mqScriptBehaviourExtensions, key);
                    if (desc) {
                        Object.defineProperty(ScriptBehaviour.prototype, key, desc);
                    }
                }
            }
            globalThis.ScriptBehaviour = ScriptBehaviour;
            if (globalThis.MQEngine) {
                globalThis.MQEngine.ScriptBehaviour = ScriptBehaviour;
            }
            if (globalThis.__nbrPackages && globalThis.__nbrPackages['mqengine']) {
                globalThis.__nbrPackages['mqengine'].ScriptBehaviour = ScriptBehaviour;
            }
            }
        )");

        m_nodeEnv->excuteScript(R"(
            globalThis.__mqAttachEntityHelpers = function(targetObj, entityId, registryPtr) {
                targetObj.entity = {
                    id: entityId,
                    registry: registryPtr
                };
                targetObj.components = globalThis.__mqCreateComponentsMapProxy(targetObj);
                for (const propName of Object.getOwnPropertyNames(ScriptBehaviour.prototype)) {
                    if (propName === 'constructor') continue;
                    const desc = Object.getOwnPropertyDescriptor(ScriptBehaviour.prototype, propName);
                    if (!desc) continue;
                    if (typeof desc.value === 'function') {
                        if (typeof targetObj[propName] !== 'function') {
                            targetObj[propName] = desc.value;
                        }
                    } else if ((desc.get || desc.set) && !(propName in targetObj)) {
                        Object.defineProperty(targetObj, propName, desc);
                    }
                }
                if (!targetObj._tickers) targetObj._tickers = {};
                targetObj._onTicker = function() {
                    const tickers = this._tickers;
                    for (let key in tickers) {
                        if (typeof tickers[key] === 'function') {
                            tickers[key].call(this);
                        }
                    }
                };
                return targetObj;
            };

            globalThis.createEntityObject = function(entityId, registryPtr) {
                return globalThis.__mqAttachEntityHelpers({}, entityId, registryPtr);
            };

            globalThis.assignGlobalFunction = function(obj, name) {
                 if (typeof globalThis[name] === 'function') {
                     if (!obj._tickers) obj._tickers = {};
                     obj._tickers[name] = globalThis[name];
                 }
            };

            globalThis.createScriptClassInstance = function(className, entityId, registryPtr) {
                const ClassConstructor = (globalThis.__mqScriptClasses && globalThis.__mqScriptClasses[className]) || globalThis[className];
                if (typeof ClassConstructor === 'function') {
                    const prevPending = globalThis.__mqPendingEntityContext;
                    globalThis.__mqPendingEntityContext = { id: entityId, registry: registryPtr };
                    try {
                        const instance = new ClassConstructor();
                        return globalThis.__mqAttachEntityHelpers(instance, entityId, registryPtr);
                    } finally {
                        globalThis.__mqPendingEntityContext = prevPending;
                    }
                } else {
                    console.error('Class not found or not a valid constructor:', className);
                    return null;
                }
            };

            globalThis.getGlobalClassNames = function() {
                const ignored = new Set([
                    'globalThis', 'console', 'process', 'entityInfo', 'engine', 'MQEngine', 'ScriptBehaviour',
                    '__mqPlugins', '__mqScriptClasses', '__mqAttachEntityHelpers', '__mqPluginRuntimeInitialized',
                    '__mqStripTypeScriptFallback', '__mqNativeStripTS', '__mqResolveComponentName',
                    '__mqCreateReactiveVecProxy', '__mqCreateReactiveComponentProxy', '__mqCreateComponentsMapProxy',
                    '__nbrDefineComponentClass', 'createEntityObject', 'assignGlobalFunction',
                    'createScriptClassInstance', 'getGlobalClassNames', 'getComponent', 'setComponent', 'hasComponent',
                    'addComponent', 'removeComponent', 'useComponent'
                ]);
                const result = new Set(Object.keys(globalThis.__mqScriptClasses || {}));
                for (const prop of Object.getOwnPropertyNames(globalThis)) {
                    if (ignored.has(prop)) continue;
                    try {
                        const val = globalThis[prop];
                        if (typeof val !== 'function') continue;
                        if (val.__isMqComponentClass || val.componentName) continue;
                        const str = val.toString();
                        if (str.includes('[native code]')) continue;
                        if (str.startsWith('class ') || (val.prototype && Object.getOwnPropertyNames(val.prototype).length > 1)) {
                            result.add(prop);
                        }
                    } catch (e) {}
                }
                return Array.from(result);
            };
        )");
    }

    void NodeJsScriptBackend::registerInputFunctions() {
        if (!m_nodeEnv) {
            return;
        }
        FCT::JS::Object globalObj = m_nodeEnv->global();
        FCT::JS::Object inputObj = globalObj["Input"];

        inputObj["isKeyPressed"] = [](int key) -> bool {
            if (g_engineGlobal.inputSystem) {
                return g_engineGlobal.inputSystem->isKeyPressed(key);
            }
            return false;
        };

        inputObj["isMouseButtonPressed"] = [](int button) -> bool {
            if (g_engineGlobal.inputSystem) {
                return g_engineGlobal.inputSystem->isMouseButtonPressed(button);
            }
            return false;
        };

        inputObj["isLeftMousePressed"] = []() -> bool {
            if (g_engineGlobal.inputSystem) {
                return g_engineGlobal.inputSystem->isLeftMousePressed();
            }
            return false;
        };

        inputObj["isRightMousePressed"] = []() -> bool {
            if (g_engineGlobal.inputSystem) {
                return g_engineGlobal.inputSystem->isRightMousePressed();
            }
            return false;
        };

        inputObj["getMousePosition"] = [this]() -> v8::Local<v8::Value> {
            if (g_engineGlobal.inputSystem) {
                FCT::Vec2 pos = g_engineGlobal.inputSystem->getMousePosition();
                return FCT::NB::toJS(*m_nodeEnv, pos);
            }
            return v8::Undefined(m_nodeEnv->isolate());
        };

        inputObj["getMouseDelta"] = [this]() -> v8::Local<v8::Value> {
            if (g_engineGlobal.inputSystem) {
                FCT::Vec2 delta = g_engineGlobal.inputSystem->getMouseDelta();
                return FCT::NB::toJS(*m_nodeEnv, delta);
            }
            return v8::Undefined(m_nodeEnv->isolate());
        };

        inputObj["getMouseWheelDelta"] = []() -> int {
            if (g_engineGlobal.inputSystem) {
                return g_engineGlobal.inputSystem->getMouseWheelDelta();
            }
            return 0;
        };
    }

    void NodeJsScriptBackend::executeSingleScriptFile(const std::string& filePath, const std::string& rawCode) {
        std::string executableCode = filePath.ends_with(".ts")
            ? transpileTypeScript(rawCode)
            : rawCode;

        bool isCommonJsModule = (filePath.find("/dist/") != std::string::npos) ||
                                (filePath.find("\\dist\\") != std::string::npos) ||
                                (executableCode.find("require(") != std::string::npos) ||
                                (executableCode.find("exports.") != std::string::npos) ||
                                (executableCode.find("module.exports") != std::string::npos) ||
                                (executableCode.find("__esModule") != std::string::npos);

        std::string autoExportSuffix = "\n";
        try {
            std::regex classRegex(R"(\bclass\s+([A-Za-z_$][A-Za-z0-9_$]*))");
            auto begin = std::sregex_iterator(executableCode.begin(), executableCode.end(), classRegex);
            auto end = std::sregex_iterator();
            for (auto it = begin; it != end; ++it) {
                std::string className = (*it)[1].str();
                autoExportSuffix += "if (typeof " + className + " === 'function') { if (typeof exports === 'object') exports." + className + " = " + className +
                                    "; globalThis." + className + " = " + className +
                                    "; globalThis.__mqScriptClasses['" + className + "'] = " + className + "; }\n";
            }
            std::regex funcRegex(R"((?:^|\n)[ \t]*function\s+([A-Za-z_$][A-Za-z0-9_$]*)\s*\()");
            auto fBegin = std::sregex_iterator(executableCode.begin(), executableCode.end(), funcRegex);
            for (auto it = fBegin; it != end; ++it) {
                std::string fnName = (*it)[1].str();
                autoExportSuffix += "if (typeof " + fnName + " === 'function') { globalThis." + fnName + " = " + fnName + "; }\n";
            }
        } catch (const std::exception&) {}

        if (isCommonJsModule) {
            std::string base64Code = m_nodeEnv->base64Encode(executableCode + autoExportSuffix);
            std::string escapedFile = EscapeJsSingleQuotedString(SAI::Path::normalize(filePath));
            std::string escapedDir = EscapeJsSingleQuotedString(SAI::Path::normalize(SAI::Path::parentPath(filePath)));

            std::string moduleWrapper = StrCat(
                "(function() {\n"
                "  const nodeModule = require('module');\n"
                "  const __filename = '", escapedFile, "';\n"
                "  const __dirname = '", escapedDir, "';\n"
                "  const localRequire = nodeModule.createRequire(__filename);\n"
                "  const module = { exports: {} };\n"
                "  const exports = module.exports;\n"
                "  const code = Buffer.from('", base64Code, "', 'base64').toString('utf8');\n"
                "  const fn = new Function('exports', 'require', 'module', '__filename', '__dirname', 'MQEngine', code);\n"
                "  fn(exports, localRequire, module, __filename, __dirname, globalThis.MQEngine);\n"
                "  const exported = module.exports;\n"
                "  if (typeof exported === 'function' && exported.name) {\n"
                "    globalThis[exported.name] = exported;\n"
                "    globalThis.__mqScriptClasses[exported.name] = exported;\n"
                "  } else if (exported && typeof exported === 'object') {\n"
                "    for (const key of Object.keys(exported)) {\n"
                "      const val = exported[key];\n"
                "      if (typeof val === 'function') {\n"
                "        const regName = (key === 'default' && val.name) ? val.name : key;\n"
                "        globalThis[regName] = val;\n"
                "        if (val.toString().startsWith('class ') || (val.prototype && Object.getOwnPropertyNames(val.prototype).length > 1)) {\n"
                "          globalThis.__mqScriptClasses[regName] = val;\n"
                "        }\n"
                "      }\n"
                "    }\n"
                "  }\n"
                "})();");
            m_nodeEnv->excuteScript(moduleWrapper);
        } else {
            m_nodeEnv->excuteScript(executableCode + autoExportSuffix);
        }
        m_nodeEnv->tick();
    }

    void NodeJsScriptBackend::executeLoadedScriptFiles(const std::vector<std::string>& scriptFiles) {
        if (!m_nodeEnv) {
            return;
        }
        for (const std::string& filePath : scriptFiles) {
            try {
                std::string rawCode = readFileContent(filePath);
                if (!rawCode.empty()) {
                    executeSingleScriptFile(filePath, rawCode);
                }
            } catch (const std::exception& e) {
                std::cerr << "Error executing script " << filePath << ": " << e.what() << std::endl;
            }
        }
    }

    std::vector<std::string> NodeJsScriptBackend::loadScriptFilesFromDirectory(const std::string& directory, const std::string& ext) {
        std::vector<std::string> scriptFiles;
        DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
        if (!dm) {
            return scriptFiles;
        }
        DataLoader* dataLoader = dm->getDataLoader();
        if (!dataLoader) {
            return scriptFiles;
        }

        try {
            dataLoader->ensureDirectory(directory);
            if (dataLoader->directoryExists(directory)) {
                scriptFiles = dataLoader->getFilePathsWithExtension(directory, ext);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error loading script files from " << directory << ": " << e.what() << std::endl;
        }
        return scriptFiles;
    }

    std::string NodeJsScriptBackend::readFileContent(const std::string& filePath) {
        DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
        if (!dm) {
            return "";
        }
        DataLoader* dataLoader = dm->getDataLoader();
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
        } catch (const std::exception& e) {
            std::cerr << "Error reading file " << filePath << ": " << e.what() << std::endl;
        }
        return "";
    }

    std::vector<std::string> NodeJsScriptBackend::getFunctionNames() const {
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

    std::vector<std::string> NodeJsScriptBackend::getClassNames() const {
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

    void NodeJsScriptBackend::setLogicDeltaTime(float deltaTime) {
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

    void NodeJsScriptBackend::tick() {
        if (m_nodeEnv) {
            try {
                m_nodeEnv->tick();
            } catch (const std::exception& e) {
                std::cerr << "Error during NodeEnvironment tick: " << e.what() << std::endl;
            }
        }
    }

    ComponentValue NodeJsScriptBackend::convertJSObjectToComponentValue(const std::string& fieldType, FCT::JS::Any& jsAny, const std::string& fieldName) {
        ComponentValue componentValue;
        try {
            v8::Local<v8::Value> rawVal = jsAny.getValue();
            if (fieldType == "int") {
                componentValue = FCT::NB::fromJS<int>(*m_nodeEnv, rawVal);
            } else if (fieldType == "float") {
                componentValue = FCT::NB::fromJS<float>(*m_nodeEnv, rawVal);
            } else if (fieldType == "double") {
                componentValue = FCT::NB::fromJS<double>(*m_nodeEnv, rawVal);
            } else if (fieldType == "bool") {
                componentValue = FCT::NB::fromJS<bool>(*m_nodeEnv, rawVal);
            } else if (fieldType == "std::string" || fieldType == "string") {
                componentValue = FCT::NB::fromJS<std::string>(*m_nodeEnv, rawVal);
            } else if (fieldType == "Vec2") {
                componentValue = FCT::NB::fromJS<FCT::Vec2>(*m_nodeEnv, rawVal);
            } else if (fieldType == "Vec3") {
                componentValue = FCT::NB::fromJS<FCT::Vec3<>>(*m_nodeEnv, rawVal);
            } else if (fieldType == "Vec4") {
                componentValue = FCT::NB::fromJS<FCT::Vec4>(*m_nodeEnv, rawVal);
            } else if (fieldType == "string[]") {
                componentValue = FCT::NB::fromJS<std::vector<std::string>>(*m_nodeEnv, rawVal);
            } else {
                throw std::runtime_error("Unsupported field type: " + fieldType);
            }
        } catch (const std::exception& e) {
            std::cerr << "Type conversion failed for field " << fieldName << " (expected type: " << fieldType << "): " << e.what() << std::endl;
            throw;
        }
        return componentValue;
    }

    v8::Local<v8::Value> NodeJsScriptBackend::convertComponentValueToJS(const ComponentValue& value) {
        return std::visit([this](const auto& v) -> v8::Local<v8::Value> {
            return FCT::NB::toJS(*m_nodeEnv, v);
        }, value);
    }

    void NodeJsScriptBackend::registerEntityFunctions() {
        auto& env = *m_nodeEnv;
        FCT::JS::Object globalObj = env.global();
        FCT::JS::Object entityInfo = globalObj["entityInfo"];

        entityInfo["getComponent"] = [this](std::string componentName) -> v8::Local<v8::Value> {
            try {
                ComponentReflection* refl = getActiveReflection();
                DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                if (!refl || !dm) return v8::Object::New(m_nodeEnv->isolate());

                FCT::JS::Object globalObj = m_nodeEnv->global();
                FCT::JS::Object entityInfo = globalObj["entityInfo"];
                uint64_t registryPtr = entityInfo["registry_ptr"];
                uint32_t entityId = entityInfo["entity_id"];

                auto registries = dm->currentRegistries();
                if (registries.empty()) return v8::Object::New(m_nodeEnv->isolate());

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    if (!refl->hasComponent(*targetRegistry, entity, componentName)) {
                        return v8::Object::New(m_nodeEnv->isolate());
                    }

                    v8::Local<v8::Object> jsObject = v8::Object::New(m_nodeEnv->isolate());
                    auto fieldNames = refl->getComponentFieldNames(componentName);
                    for (const auto& fieldName : fieldNames) {
                        try {
                            auto value = refl->getComponentField(*targetRegistry, entity, componentName, fieldName);
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
                ComponentReflection* refl = getActiveReflection();
                DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                if (!refl || !dm) return false;

                FCT::JS::Object globalObj = m_nodeEnv->global();
                FCT::JS::Object entityInfo = globalObj["entityInfo"];
                uint64_t registryPtr = entityInfo["registry_ptr"];
                uint32_t entityId = entityInfo["entity_id"];

                auto registries = dm->currentRegistries();
                if (registries.empty()) return false;

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    auto fieldNames = refl->getComponentFieldNames(componentName);
                    for (const auto& fieldName : fieldNames) {
                        if (componentObj.hasProperty(fieldName)) {
                            std::string fieldType = refl->getComponentFieldType(componentName, fieldName);
                            try {
                                FCT::JS::Any fieldValue = componentObj.get<FCT::JS::Any>(fieldName);
                                ComponentValue componentValue = convertJSObjectToComponentValue(fieldType, fieldValue, fieldName);
                                refl->setComponentField(*targetRegistry, entity, componentName, fieldName, componentValue);
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
                ComponentReflection* refl = getActiveReflection();
                DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                if (!refl || !dm) return v8::Undefined(m_nodeEnv->isolate());

                FCT::JS::Object globalObj = m_nodeEnv->global();
                FCT::JS::Object entityInfo = globalObj["entityInfo"];
                uint64_t registryPtr = entityInfo["registry_ptr"];
                uint32_t entityId = entityInfo["entity_id"];

                auto registries = dm->currentRegistries();
                if (registries.empty()) return v8::Undefined(m_nodeEnv->isolate());

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    auto value = refl->getComponentField(*targetRegistry, entity, componentName, fieldName);
                    return convertComponentValueToJS(value);
                }
            } catch (const std::exception& e) {
                std::cerr << "Error in entityInfo.getComponentField: " << e.what() << std::endl;
            }
            return v8::Undefined(m_nodeEnv->isolate());
        };

        entityInfo["setComponentField"] = [this](std::string componentName, std::string fieldName, FCT::JS::Any fieldValue) -> bool {
            try {
                ComponentReflection* refl = getActiveReflection();
                DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                if (!refl || !dm) return false;

                FCT::JS::Object globalObj = m_nodeEnv->global();
                FCT::JS::Object entityInfo = globalObj["entityInfo"];
                uint64_t registryPtr = entityInfo["registry_ptr"];
                uint32_t entityId = entityInfo["entity_id"];

                auto registries = dm->currentRegistries();
                if (registries.empty()) return false;

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    std::string fieldType = refl->getComponentFieldType(componentName, fieldName);
                    try {
                        ComponentValue componentValue = convertJSObjectToComponentValue(fieldType, fieldValue, fieldName);
                        refl->setComponentField(*targetRegistry, entity, componentName, fieldName, componentValue);
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
                ComponentReflection* refl = getActiveReflection();
                DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                if (!refl || !dm) return false;

                FCT::JS::Object globalObj = m_nodeEnv->global();
                FCT::JS::Object entityInfo = globalObj["entityInfo"];
                uint64_t registryPtr = entityInfo["registry_ptr"];
                uint32_t entityId = entityInfo["entity_id"];

                auto registries = dm->currentRegistries();
                if (registries.empty()) return false;

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    return refl->hasComponent(*targetRegistry, entity, componentName);
                }
            } catch (const std::exception& e) {
                std::cerr << "Error in entityInfo.hasComponent: " << e.what() << std::endl;
            }
            return false;
        };

        entityInfo["addComponent"] = [this](std::string componentName) -> bool {
            try {
                ComponentReflection* refl = getActiveReflection();
                DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                if (!refl || !dm) return false;

                FCT::JS::Object globalObj = m_nodeEnv->global();
                FCT::JS::Object entityInfo = globalObj["entityInfo"];
                uint64_t registryPtr = entityInfo["registry_ptr"];
                uint32_t entityId = entityInfo["entity_id"];

                auto registries = dm->currentRegistries();
                if (registries.empty()) return false;

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    refl->addComponent(*targetRegistry, entity, componentName);
                    return true;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error in entityInfo.addComponent: " << e.what() << std::endl;
            }
            return false;
        };

        entityInfo["removeComponent"] = [this](std::string componentName) -> bool {
            try {
                ComponentReflection* refl = getActiveReflection();
                DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                if (!refl || !dm) return false;

                FCT::JS::Object globalObj = m_nodeEnv->global();
                FCT::JS::Object entityInfo = globalObj["entityInfo"];
                uint64_t registryPtr = entityInfo["registry_ptr"];
                uint32_t entityId = entityInfo["entity_id"];

                auto registries = dm->currentRegistries();
                if (registries.empty()) return false;

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    refl->removeComponent(*targetRegistry, entity, componentName);
                    return true;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error in entityInfo.removeComponent: " << e.what() << std::endl;
            }
            return false;
        };

        entityInfo["getComponentById"] = [this](double registryPtrDouble, uint32_t entityId, std::string componentName) -> v8::Local<v8::Value> {
            try {
                ComponentReflection* refl = getActiveReflection();
                DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                if (!refl || !dm) return v8::Undefined(m_nodeEnv->isolate());

                uint64_t registryPtr = static_cast<uint64_t>(registryPtrDouble);
                auto registries = dm->currentRegistries();
                if (registries.empty()) return v8::Undefined(m_nodeEnv->isolate());

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    if (!refl->hasComponent(*targetRegistry, entity, componentName)) {
                        return v8::Undefined(m_nodeEnv->isolate());
                    }

                    v8::Local<v8::Object> jsObject = v8::Object::New(m_nodeEnv->isolate());
                    auto fieldNames = refl->getComponentFieldNames(componentName);
                    for (const auto& fieldName : fieldNames) {
                        try {
                            auto value = refl->getComponentField(*targetRegistry, entity, componentName, fieldName);
                            v8::Local<v8::Value> jsValue = convertComponentValueToJS(value);
                            jsObject->Set(m_nodeEnv->context(), convertToJS(*m_nodeEnv, fieldName).As<v8::String>(), jsValue);
                        } catch (const std::exception&) {
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
                ComponentReflection* refl = getActiveReflection();
                DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                if (!refl || !dm) return false;

                uint64_t registryPtr = static_cast<uint64_t>(registryPtrDouble);
                auto registries = dm->currentRegistries();
                if (registries.empty()) return false;

                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                auto it = std::find(registries.begin(), registries.end(), targetRegistry);
                if (it != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    if (!refl->hasComponent(*targetRegistry, entity, componentName)) {
                        refl->addComponent(*targetRegistry, entity, componentName);
                    }
                    auto fieldNames = refl->getComponentFieldNames(componentName);
                    for (const auto& fieldName : fieldNames) {
                        if (componentObj.hasProperty(fieldName)) {
                            std::string fieldType = refl->getComponentFieldType(componentName, fieldName);
                            try {
                                FCT::JS::Any fieldValue = componentObj.get<FCT::JS::Any>(fieldName);
                                ComponentValue componentValue = convertJSObjectToComponentValue(fieldType, fieldValue, fieldName);
                                refl->setComponentField(*targetRegistry, entity, componentName, fieldName, componentValue);
                            } catch (const std::exception&) {
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

        entityInfo["hasComponentById"] = [this](double registryPtrDouble, uint32_t entityId, std::string componentName) -> bool {
            try {
                ComponentReflection* refl = getActiveReflection();
                DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                if (!refl || !dm) return false;
                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(static_cast<uint64_t>(registryPtrDouble));
                auto registries = dm->currentRegistries();
                if (std::find(registries.begin(), registries.end(), targetRegistry) != registries.end()) {
                    return refl->hasComponent(*targetRegistry, static_cast<entt::entity>(entityId), componentName);
                }
            } catch (const std::exception&) {}
            return false;
        };

        entityInfo["addComponentById"] = [this](double registryPtrDouble, uint32_t entityId, std::string componentName) -> bool {
            try {
                ComponentReflection* refl = getActiveReflection();
                DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                if (!refl || !dm) return false;
                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(static_cast<uint64_t>(registryPtrDouble));
                auto registries = dm->currentRegistries();
                if (std::find(registries.begin(), registries.end(), targetRegistry) != registries.end()) {
                    refl->addComponent(*targetRegistry, static_cast<entt::entity>(entityId), componentName);
                    return true;
                }
            } catch (const std::exception&) {}
            return false;
        };

        entityInfo["removeComponentById"] = [this](double registryPtrDouble, uint32_t entityId, std::string componentName) -> bool {
            try {
                ComponentReflection* refl = getActiveReflection();
                DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                if (!refl || !dm) return false;
                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(static_cast<uint64_t>(registryPtrDouble));
                auto registries = dm->currentRegistries();
                if (std::find(registries.begin(), registries.end(), targetRegistry) != registries.end()) {
                    refl->removeComponent(*targetRegistry, static_cast<entt::entity>(entityId), componentName);
                    return true;
                }
            } catch (const std::exception&) {}
            return false;
        };

        entityInfo["getComponentFieldById"] = [this](double registryPtrDouble, uint32_t entityId, std::string componentName, std::string fieldName) -> v8::Local<v8::Value> {
            try {
                ComponentReflection* refl = getActiveReflection();
                DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                if (!refl || !dm) return v8::Undefined(m_nodeEnv->isolate());
                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(static_cast<uint64_t>(registryPtrDouble));
                auto registries = dm->currentRegistries();
                if (std::find(registries.begin(), registries.end(), targetRegistry) != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    if (!refl->hasComponent(*targetRegistry, entity, componentName)) {
                        return v8::Undefined(m_nodeEnv->isolate());
                    }
                    auto value = refl->getComponentField(*targetRegistry, entity, componentName, fieldName);
                    return convertComponentValueToJS(value);
                }
            } catch (const std::exception&) {}
            return v8::Undefined(m_nodeEnv->isolate());
        };

        entityInfo["setComponentFieldById"] = [this](double registryPtrDouble, uint32_t entityId, std::string componentName, std::string fieldName, FCT::JS::Any fieldValue) -> bool {
            try {
                ComponentReflection* refl = getActiveReflection();
                DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                if (!refl || !dm) return false;
                entt::registry* targetRegistry = reinterpret_cast<entt::registry*>(static_cast<uint64_t>(registryPtrDouble));
                auto registries = dm->currentRegistries();
                if (std::find(registries.begin(), registries.end(), targetRegistry) != registries.end()) {
                    entt::entity entity = static_cast<entt::entity>(entityId);
                    if (!refl->hasComponent(*targetRegistry, entity, componentName)) {
                        refl->addComponent(*targetRegistry, entity, componentName);
                    }
                    std::string fieldType = refl->getComponentFieldType(componentName, fieldName);
                    if (fieldType.empty()) return false;
                    ComponentValue componentValue = convertJSObjectToComponentValue(fieldType, fieldValue, fieldName);
                    refl->setComponentField(*targetRegistry, entity, componentName, fieldName, componentValue);
                    return true;
                }
            } catch (const std::exception&) {}
            return false;
        };
    }

    namespace {
        std::string StripTypeScriptInterfaces(const std::string& input) {
            std::istringstream iss(input);
            std::ostringstream oss;
            std::string line;
            int skipBraceDepth = 0;
            bool waitingForOpenBrace = false;

            while (std::getline(iss, line)) {
                if (skipBraceDepth == 0 && !waitingForOpenBrace) {
                    size_t firstNonSpace = line.find_first_not_of(" \t\r");
                    if (firstNonSpace != std::string::npos) {
                        std::string_view trimmed(line.data() + firstNonSpace, line.size() - firstNonSpace);
                        bool isInterfaceLine = false;
                        if (trimmed.starts_with("interface ") || trimmed.starts_with("export interface ")) {
                            isInterfaceLine = true;
                        }
                        if (isInterfaceLine) {
                            waitingForOpenBrace = true;
                            for (char c : trimmed) {
                                if (c == '{') {
                                    skipBraceDepth++;
                                    waitingForOpenBrace = false;
                                } else if (c == '}' && !waitingForOpenBrace) {
                                    skipBraceDepth--;
                                }
                            }
                            continue;
                        }
                    }
                    oss << line << "\n";
                } else {
                    for (char c : line) {
                        if (c == '{') {
                            skipBraceDepth++;
                            waitingForOpenBrace = false;
                        } else if (c == '}' && !waitingForOpenBrace) {
                            skipBraceDepth--;
                        }
                    }
                    if (skipBraceDepth <= 0 && !waitingForOpenBrace) {
                        skipBraceDepth = 0;
                    }
                }
            }
            return oss.str();
        }
    }

    std::string NodeJsScriptBackend::transpileTypeScript(const std::string& tsSource) {
        std::string strippedInterfaces = StripTypeScriptInterfaces(tsSource);
        std::istringstream iss(strippedInterfaces);
        std::ostringstream oss;
        std::string line;

        while (std::getline(iss, line)) {
            try {
                if (std::regex_match(line, std::regex(R"(^[ \t]*import[ \t]+type[ \t]+.*$)"))) {
                    continue;
                }
                if (std::regex_match(line, std::regex(R"(^[ \t]*(?:export[ \t]+)?type[ \t]+[A-Za-z0-9_]+[ \t]*=.*$)"))) {
                    continue;
                }
                line = std::regex_replace(
                    line,
                    std::regex(R"(^([ \t]*)import[ \t]*\{([^\r\n\}]+)\}[ \t]*from[ \t]*(['"][^\r\n'"]+['"])[ \t]*;?[\r\t ]*$)"),
                    "$1const {$2} = require($3);");
                line = std::regex_replace(
                    line,
                    std::regex(R"(^([ \t]*)import[ \t]+\*[ \t]+as[ \t]+([A-Za-z_$][A-Za-z0-9_$]*)[ \t]+from[ \t]+(['"][^\r\n'"]+['"])[ \t]*;?[\r\t ]*$)"),
                    "$1const $2 = require($3);");
                line = std::regex_replace(line, std::regex(R"(^([ \t]*)export[ \t]+default[ \t]+)"), "$1module.exports = ");
                line = std::regex_replace(line, std::regex(R"(^([ \t]*)export[ \t]+(class|function|const|let|var)\b)"), "$1$2");
                line = std::regex_replace(
                    line,
                    std::regex(R"(\b(class\s+[A-Za-z_$][A-Za-z0-9_$]*(?:\s+extends\s+[A-Za-z0-9_$\.]+)?)\s+implements\s+[^{]+(\{?))"),
                    "$1 $2");
                if (std::regex_match(line, std::regex(R"(^[ \t]*[A-Za-z_$][A-Za-z0-9_$]*[ \t]*[!\?][ \t]*:[^=]+;[\r\t ]*$)"))) {
                    continue;
                }
                if (std::regex_match(line, std::regex(R"(^[ \t]*(?!default\b)[A-Za-z_$][A-Za-z0-9_$]*[ \t]*:[ \t]*[A-Za-z_$][A-Za-z0-9_<>\[\]\.\|\&, \t]*;[\r\t ]*$)"))) {
                    continue;
                }
                line = std::regex_replace(
                    line,
                    std::regex(R"(^([ \t]*[A-Za-z_$][A-Za-z0-9_$]*)[ \t]*[!\?]?[ \t]*:[ \t]*[A-Za-z_$][A-Za-z0-9_<>\[\]\.\|\&, \t]*([ \t]*=))"),
                    "$1$2");
                line = std::regex_replace(
                    line,
                    std::regex(R"(\)[ \t]*:[ \t]*[A-Za-z_$][A-Za-z0-9_<>\[\]\.\|\&, \t]*(\s*(?:\{|=>|$)))"),
                    ")$1");
                line = std::regex_replace(
                    line,
                    std::regex(R"(\b(const|let|var)\s+([A-Za-z_$][A-Za-z0-9_$]*)[ \t]*:[ \t]*[A-Za-z_$][A-Za-z0-9_<>\[\]\.\|\&, \t]*([ \t]*=))"),
                    "$1 $2$3");
                line = std::regex_replace(
                    line,
                    std::regex(R"(([\(\,]\s*[A-Za-z_$][A-Za-z0-9_$]*)\??\s*:\s*(?:string|number|boolean|any|unknown|void|never|object|Record<[^>]+>|Array<[^>]+>|[A-Za-z_$][A-Za-z0-9_$\.]*(?:\[\])?)(?=\s*[,\)\=]))"),
                    "$1");
                line = std::regex_replace(
                    line,
                    std::regex(R"(\s+as\s+(?:any|unknown|string|number|boolean|Record<[^>]+>|[A-Za-z_$][A-Za-z0-9_$\.]*(?:\[\])?)\b)"),
                    "");
            } catch (const std::exception&) {
            }
            oss << line << "\n";
        }

        return oss.str();
    }

    void NodeJsScriptBackend::setupNbrEnginePackage() {
        if (!m_nodeEnv || !m_enginePackage) {
            return;
        }

        m_enginePackage->registerStruct<FCT::Vec2>("Vec2");
        m_enginePackage->registerStruct<FCT::Vec3<>>("Vec3");
        m_enginePackage->registerStruct<FCT::Vec4>("Vec4");
        m_enginePackage->registerStruct<ProjectConfig>("ProjectConfig");
        m_enginePackage->registerComponentTuple<AllComponentsList>();

        if (Engine::getInstance()) {
            m_enginePackage->bindDescribedService(*m_nodeEnv, "Engine", Engine::getInstance());
            if (true) {
                m_enginePackage->bindDescribedService(*m_nodeEnv, "Foundation", Engine::getInstance()->foundation.get());
            }
        }
        m_enginePackage->bindDescribedService(*m_nodeEnv, "Runtime", &EngineRuntime::instance());
        m_enginePackage->bindDescribedService(*m_nodeEnv, "StateMachine", &EngineRuntime::instance());

        DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
        if (dm) {
            m_enginePackage->bindDescribedService(*m_nodeEnv, "DataManager", dm);
        }

        if (ComponentReflection* refl = getActiveReflection()) {
            m_enginePackage->bindDescribedService(*m_nodeEnv, "ComponentReflection", refl);
        }

        if (g_engineGlobal.memoryManager) {
            m_enginePackage->bindDescribedService(*m_nodeEnv, "MemoryManager", g_engineGlobal.memoryManager);
        }
        if (g_engineGlobal.jobSystem) {
            m_enginePackage->bindDescribedService(*m_nodeEnv, "JobSystem", g_engineGlobal.jobSystem);
        }
        if (g_engineGlobal.skeletalAnimationSystem) {
            m_enginePackage->bindDescribedService(*m_nodeEnv, "SkeletalAnimation", g_engineGlobal.skeletalAnimationSystem);
        }
        if (g_engineGlobal.networkManager) {
            m_enginePackage->bindDescribedService(*m_nodeEnv, "Network", g_engineGlobal.networkManager);
        }
        if (g_engineGlobal.networkSyncSystem) {
            m_enginePackage->bindDescribedService(*m_nodeEnv, "NetworkSync", g_engineGlobal.networkSyncSystem);
        }

        if (g_engineGlobal.inputSystem) {
            m_enginePackage->bindDescribedService(*m_nodeEnv, "Input", g_engineGlobal.inputSystem);
        } else {
            FCT::JS::Object inputServiceObj = m_nodeEnv->createJSObject();
            FCT::NB::bindFunctionToObject(*m_nodeEnv, inputServiceObj, "isKeyPressed", [](int key) -> bool {
                return g_engineGlobal.inputSystem ? g_engineGlobal.inputSystem->isKeyPressed(key) : false;
            });
            FCT::NB::bindFunctionToObject(*m_nodeEnv, inputServiceObj, "getMousePosition", []() -> FCT::Vec2 {
                return g_engineGlobal.inputSystem ? g_engineGlobal.inputSystem->getMousePosition() : FCT::Vec2(0.0f, 0.0f);
            });
            m_enginePackage->bindServiceObject(
                *m_nodeEnv,
                "Input",
                inputServiceObj,
                "    isKeyPressed(key: number): boolean;\n    getMousePosition(): Vec2;\n");
        }

        m_enginePackage->addTypeScriptDeclaration(R"TS(export interface EntityHandle {
    id: number;
    registry: number;
}

export class ScriptBehaviour {
    entity: EntityHandle;
    components: ComponentMap & Record<string, any>;
    position: Vec3;
    rotation: Vec3;
    scale: Vec3;
    readonly deltaTime: number;
    getComponent<T>(compClass: ComponentClass<T>): T | undefined;
    getComponent<K extends ComponentName>(name: K): ComponentMap[K] | undefined;
    getComponent<T = any>(name: string): T | undefined;
    setComponent<T>(compClass: ComponentClass<T>, value: Partial<T>): boolean;
    setComponent<K extends ComponentName>(name: K, value: Partial<ComponentMap[K]>): boolean;
    setComponent<T = any>(name: string, value: Partial<T>): boolean;
    hasComponent<T>(compClass: ComponentClass<T>): boolean;
    hasComponent<K extends ComponentName>(name: K): boolean;
    hasComponent(name: string): boolean;
    addComponent<T>(compClass: ComponentClass<T>, initValue?: Partial<T>): T | undefined;
    addComponent<K extends ComponentName>(name: K, initValue?: Partial<ComponentMap[K]>): ComponentMap[K] | undefined;
    addComponent<T = any>(name: string, initValue?: Partial<T>): T | undefined;
    removeComponent<T>(compClass: ComponentClass<T>): boolean;
    removeComponent<K extends ComponentName>(name: K): boolean;
    removeComponent(name: string): boolean;
    useComponent<T>(compClass: ComponentClass<T>, autoAdd?: boolean): T;
    useComponent<K extends ComponentName>(name: K, autoAdd?: boolean): ComponentMap[K];
    useComponent<T = any>(name: string, autoAdd?: boolean): T;
    setParent(parentEntityId: number, trunkName?: string): boolean;
    clearParent(trunkName?: string): boolean;
    getParent(trunkName?: string): number;
    getChildren(trunkName?: string): number[];
    getWorldPosition(trunkName?: string): Vec3;
    setWorldPosition(worldPos: Vec3, trunkName?: string): boolean;
    getPositionEngine(trunkName?: string): Vec3;
    setPositionEngine(enginePos: Vec3, trunkName?: string): boolean;
    lookAt(target: Vec3, trunkName?: string): boolean;
    onStart?(): void;
    onUpdate?(dt: number): void;
    onTicker?(): void;
}

export interface ScriptInstanceBase extends ScriptBehaviour {}

export function registerPlugin(pluginDef: Record<string, any>): Record<string, any>;)TS");

        m_enginePackage->mountToEnvironment(*m_nodeEnv, "MQEngine");
    }

    void NodeJsScriptBackend::ensureScriptsWorkspaceConfig(const std::string& scriptsRoot, ResourceLoader* loader) {
        if (!loader) {
            return;
        }
        (void)loader->ensureDir(scriptsRoot + "/src");
        (void)loader->ensureDir(scriptsRoot + "/dist");

        const std::string pkgJsonPath = scriptsRoot + "/package.json";
        if (!loader->loadFile(pkgJsonPath).ok()) {
            static const char* kDefaultScriptsPackageJson = R"JSON({
  "name": "mqengine-game-scripts",
  "version": "1.0.0",
  "private": true,
  "description": "MQEngine TypeScript/JavaScript Game Scripts",
  "scripts": {
    "build": "tsc",
    "watch": "tsc --watch"
  },
  "dependencies": {
    "mqengine": "file:./node_modules/mqengine"
  },
  "devDependencies": {
    "typescript": "^5.6.0"
  }
}
)JSON";
            (void)loader->saveFile(pkgJsonPath, kDefaultScriptsPackageJson);
        }

        const std::string tsconfigPath = scriptsRoot + "/tsconfig.json";
        if (!loader->loadFile(tsconfigPath).ok()) {
            static const char* kDefaultTsConfig = R"JSON({
  "compilerOptions": {
    "target": "ES2022",
    "module": "CommonJS",
    "moduleResolution": "node",
    "rootDir": "./src",
    "outDir": "./dist",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "forceConsistentCasingInFileNames": true,
    "declaration": false,
    "sourceMap": false,
    "baseUrl": ".",
    "paths": {
      "mqengine": ["./node_modules/mqengine"]
    }
  },
  "include": ["src/**/*.ts", "node_modules/mqengine/index.d.ts"],
  "exclude": ["dist", "node_modules"]
}
)JSON";
            (void)loader->saveFile(tsconfigPath, kDefaultTsConfig);
        }

        const std::string sampleTsPath = scriptsRoot + "/src/SpinBehaviour.ts";
        if (!loader->loadFile(sampleTsPath).ok()) {
            static const char* kSampleTs = R"TS(import { ScriptBehaviour, RotationComponent } from "mqengine";

export class SpinBehaviour extends ScriptBehaviour {
    speed: number = 45.0;

    onStart(): void {
        console.log(`[SpinBehaviour] 实体 ${this.entity.id} 脚本已启动`);
    }

    onUpdate(dt: number): void {
        const rot = this.getComponent(RotationComponent);
        if (rot) {
            rot.rotation.y += this.speed * dt;
        }
    }
}
)TS";
            (void)loader->saveFile(sampleTsPath, kSampleTs);
        }

        const std::string sampleJsPath = scriptsRoot + "/dist/SpinBehaviour.js";
        if (!loader->loadFile(sampleJsPath).ok()) {
            static const char* kSampleJs = R"JS("use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.SpinBehaviour = void 0;
const mqengine_1 = require("mqengine");
class SpinBehaviour extends mqengine_1.ScriptBehaviour {
    constructor() {
        super(...arguments);
        this.speed = 45.0;
    }
    onStart() {
        console.log(`[SpinBehaviour] 实体 ${this.entity.id} 脚本已启动`);
    }
    onUpdate(dt) {
        const rot = this.getComponent(mqengine_1.RotationComponent);
        if (rot) {
            rot.rotation.y += this.speed * dt;
        }
    }
}
exports.SpinBehaviour = SpinBehaviour;
)JS";
            (void)loader->saveFile(sampleJsPath, kSampleJs);
        }
    }

    void NodeJsScriptBackend::exportTypeDefinitions(const std::string& scriptsRoot) {
        if (!m_enginePackage) {
            return;
        }

        ResourceLoader* loader = g_engineGlobal.resourceLoader;
        UniquePtr<DiskResourceLoader> fallbackLoader;
        if (!loader) {
            fallbackLoader = makeUnique<DiskResourceLoader>();
            loader = fallbackLoader.get();
        }

        const std::string pkgDir = scriptsRoot + "/node_modules/mqengine";
        (void)loader->ensureDir(pkgDir);
        (void)loader->saveFile(pkgDir + "/package.json", m_enginePackage->generatePackageJson());
        (void)loader->saveFile(pkgDir + "/index.js", m_enginePackage->generateIndexJs());
        (void)loader->saveFile(pkgDir + "/index.d.ts", m_enginePackage->generateTypeScriptDts("MQEngine"));

        ensureScriptsWorkspaceConfig(scriptsRoot, loader);
    }

    void NodeJsScriptBackend::initializeCoreBridge(const std::string& scriptsRoot) {
        if (!m_nodeEnv) {
            return;
        }

        setupNbrEnginePackage();
        exportTypeDefinitions(scriptsRoot);

        if (m_isCoreBridgeInitialized) {
            m_nodeEnv->excuteScript(R"JS(
                globalThis.engine = globalThis.engine || { logicDealt: 0.0 };
                if (globalThis.MQEngine && globalThis.MQEngine.Engine) {
                    Object.assign(globalThis.engine, globalThis.MQEngine.Engine);
                }
            )JS");
            return;
        }

        FCT::JS::Object globalObj = m_nodeEnv->global();
        globalObj["__mqNativeStripTS"] = [](std::string tsCode) -> std::string {
            return NodeJsScriptBackend::transpileTypeScript(tsCode);
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
                globalThis.MQEngine.registerPlugin = function(pluginDef) {
                    if (pluginDef && pluginDef.name) {
                        globalThis.__mqPlugins[pluginDef.name] = pluginDef;
                    }
                    return pluginDef;
                };

                globalThis.engine = globalThis.engine || { logicDealt: 0.0 };
                if (globalThis.MQEngine && globalThis.MQEngine.Engine) {
                    Object.assign(globalThis.engine, globalThis.MQEngine.Engine);
                }
            }
        )JS";

        try {
            m_nodeEnv->excuteScript(kCoreBridgeBootstrap);
            m_nodeEnv->tick();
            m_isCoreBridgeInitialized = true;
        } catch (const std::exception& e) {
            spdlog::error("NodeJsScriptBackend 初始化核心 JS/TS 桥接失败: {}", e.what());
        }
    }

    Status NodeJsScriptBackend::executeModuleScript(const std::string& moduleName,
                                                    const std::string& moduleVersion,
                                                    const std::string& entryPath,
                                                    const std::string& moduleDir,
                                                    ResourceLoader* resourceLoader) {
        if (!m_nodeEnv) {
            return FailedPreconditionError("NodeEnvironment 未初始化");
        }

        initializeCoreBridge("./Assets/Scripts");

        UniquePtr<DiskResourceLoader> fallbackLoader;
        if (!resourceLoader) {
            fallbackLoader = makeUnique<DiskResourceLoader>();
            resourceLoader = fallbackLoader.get();
        }

        auto scriptRes = resourceLoader->loadFile(entryPath);
        CHECK_STATUS(scriptRes);

        const std::string& rawSource = scriptRes.value();
        bool isTypeScript = entryPath.ends_with(".ts");
        std::string precompiledSource = isTypeScript ? transpileTypeScript(rawSource) : rawSource;

        std::string base64Source = m_nodeEnv->base64Encode(precompiledSource);
        std::string escapedName = EscapeJsSingleQuotedString(moduleName);
        std::string escapedVersion = EscapeJsSingleQuotedString(moduleVersion);
        std::string escapedFile = EscapeJsSingleQuotedString(entryPath);
        std::string escapedDir = EscapeJsSingleQuotedString(moduleDir);

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
            return InternalError(StrCat("执行脚本模块 [", moduleName, "] 失败: ", e.what()));
        }
        return OkStatus();
    }

    Status NodeJsScriptBackend::invokeModuleLifecycle(const std::string& moduleName,
                                                      const std::string& methodName,
                                                      const std::string& argsJson) {
        if (!m_nodeEnv) {
            return FailedPreconditionError("NodeEnvironment 未初始化");
        }

        std::string escapedName = EscapeJsSingleQuotedString(moduleName);
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
            return InternalError(StrCat("调用脚本模块 [", moduleName, "::", methodName, "] 异常: ", e.what()));
        }
        return OkStatus();
    }

    void NodeJsScriptBackend::unloadModuleScript(const std::string& moduleName) {
        if (!m_nodeEnv) {
            return;
        }
        (void)invokeModuleLifecycle(moduleName, "onUnload");
        try {
            std::string escapedName = EscapeJsSingleQuotedString(moduleName);
            m_nodeEnv->excuteScript(
                "if (globalThis.__mqPlugins) { delete globalThis.__mqPlugins['" + escapedName + "']; }");
            m_nodeEnv->tick();
        } catch (...) {
        }
    }

} // namespace MQEngine
