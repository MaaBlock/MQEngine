#include "PybindScriptBackend.h"
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
#include <algorithm>
#include <iostream>
#include <sstream>

namespace MQEngine {

    using absl::FailedPreconditionError;
    using Runtime::Engine;
    using EngineRuntime = Runtime::Runtime;

    namespace {

    std::string EscapePythonSingleQuotedString(const std::string& input) {
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

    float ParseDeltaTimeFromArgsJson(const std::string& argsJson, float fallbackDt) {
        if (argsJson.empty() || argsJson == "{}") {
            return fallbackDt > 0.0f ? fallbackDt : 0.016f;
        }
        boost::system::error_code ec;
        boost::json::value jv = boost::json::parse(argsJson, ec);
        if (!ec && jv.is_object()) {
            const auto& obj = jv.as_object();
            if (auto it = obj.find("dt"); it != obj.end() && it->value().is_number()) {
                return static_cast<float>(it->value().to_number<double>());
            }
        }
        return fallbackDt > 0.0f ? fallbackDt : 0.016f;
    }

    } // namespace

    PybindScriptBackend::PybindScriptBackend(DataManager* dataManager)
        : m_dataManager(dataManager)
        , m_fallbackReflection(makeUnique<ComponentReflection>())
#ifndef __ANDROID__
        , m_pyEnv(makeUnique<FCT::PyBri::PyEnvironment>())
        , m_enginePackage(makeUnique<FCT::PyBri::Package>("mqengine"))
#endif
    {
    }

    PybindScriptBackend::~PybindScriptBackend() {
        shutdown();
    }

    ComponentReflection* PybindScriptBackend::getActiveReflection() const {
        if (Engine::getInstance() && Engine::getInstance()->foundation && Engine::getInstance()->foundation->getComponentReflection()) {
            return Engine::getInstance()->foundation->getComponentReflection();
        }
        return m_fallbackReflection.get();
    }

    Status PybindScriptBackend::initialize() {
#ifndef __ANDROID__
        if (!m_pyEnv) {
            return FailedPreconditionError("PyEnvironment 实例创建失败");
        }
        m_pyEnv->addModulePath("./Assets/Scripts");
        m_pyEnv->addModulePath("./Assets/Scripts/py");
        if (!m_pyEnv->setup()) {
            m_isAvailable = false;
            return InternalError("PyEnvironment 初始化失败");
        }
        m_isAvailable = true;
        initializeCoreBridge("./Assets/Scripts");
        injectEntityRuntimeBootstrap();
        registerEntityFunctions();
        registerInputFunctions();
        return OkStatus();
#else
        m_isAvailable = false;
        return OkStatus();
#endif
    }

    void PybindScriptBackend::shutdown() {
#ifndef __ANDROID__
        m_enginePackage.reset();
        if (m_pyEnv) {
            m_pyEnv->stop();
            m_pyEnv.reset();
        }
#endif
        {
            std::lock_guard<std::mutex> lock(m_lifecycleCacheMutex);
            m_moduleLifecycleMethods.clear();
        }
        m_isAvailable = false;
        m_isCoreBridgeInitialized = false;
    }

    Status PybindScriptBackend::loadScriptsFromRoot(const std::string& scriptsRoot) {
#ifndef __ANDROID__
        if (!m_pyEnv || !m_isAvailable) {
            return FailedPreconditionError("PyEnvironment 未初始化");
        }

        try {
            m_pyEnv->addModulePath(scriptsRoot);
            m_pyEnv->addModulePath(scriptsRoot + "/py");
            m_pyEnv->addModulePath(scriptsRoot + "/src");

            initializeCoreBridge(scriptsRoot);
            exportTypeDefinitions(scriptsRoot);
            injectEntityRuntimeBootstrap();
            registerEntityFunctions();
            registerInputFunctions();

            std::vector<std::string> pyFiles = loadScriptFilesFromDirectory(scriptsRoot + "/py/", ".py");
            std::vector<std::string> srcPyFiles = loadScriptFilesFromDirectory(scriptsRoot + "/src/", ".py");

            std::vector<std::string> allPyFiles;
            allPyFiles.insert(allPyFiles.end(), pyFiles.begin(), pyFiles.end());
            allPyFiles.insert(allPyFiles.end(), srcPyFiles.begin(), srcPyFiles.end());

            for (const std::string& filePath : allPyFiles) {
                std::string fileName = SAI::Path::fileName(filePath);
                if (fileName == "__init__.py" || fileName.ends_with(".pyi")) {
                    continue;
                }
                std::string rawCode = readFileContent(filePath);
                if (!rawCode.empty()) {
                    executeSinglePythonFile(filePath, rawCode);
                }
            }
            spdlog::info("PybindScriptBackend (FCT_PyBri) 已从 [{}] 加载并执行 {} 个 Python 脚本文件 (.py)",
                         scriptsRoot, allPyFiles.size());
        } catch (const std::exception& e) {
            return InternalError(StrCat("加载 Python 脚本异常: ", e.what()));
        }
#else
        (void)scriptsRoot;
#endif
        return OkStatus();
    }

    Status PybindScriptBackend::reloadScriptsFromRoot(const std::string& scriptsRoot) {
        return loadScriptsFromRoot(scriptsRoot);
    }

    void PybindScriptBackend::exportTypeDefinitions(const std::string& scriptsRoot) {
#ifndef __ANDROID__
        if (!m_enginePackage) {
            return;
        }

        ResourceLoader* loader = g_engineGlobal.resourceLoader;
        UniquePtr<DiskResourceLoader> fallbackLoader;
        if (!loader) {
            fallbackLoader = makeUnique<DiskResourceLoader>();
            loader = fallbackLoader.get();
        }

        const std::string pyDir = scriptsRoot + "/py";
        (void)loader->ensureDir(pyDir);

        std::string pyiContent = m_enginePackage->generatePythonPyi("MQEngine");
        (void)loader->saveFile(pyDir + "/mqengine.pyi", pyiContent);
        (void)loader->saveFile(scriptsRoot + "/mqengine.pyi", pyiContent);

        ensurePythonWorkspaceConfig(scriptsRoot, loader);
#else
        (void)scriptsRoot;
#endif
    }

    void PybindScriptBackend::ensurePythonWorkspaceConfig(const std::string& scriptsRoot, ResourceLoader* loader) {
        if (!loader) {
            return;
        }
        const std::string pyDir = scriptsRoot + "/py";
        (void)loader->ensureDir(pyDir);

        const std::string samplePyPath = pyDir + "/SpinBehaviourPy.py";
        if (!loader->loadFile(samplePyPath).ok()) {
            static const char* kSamplePy = R"PY(from mqengine import ScriptBehaviour, RotationComponent


class SpinBehaviourPy(ScriptBehaviour):
    def __init__(self):
        super().__init__()
        self.speed = 60.0

    def on_start(self):
        print(f"[SpinBehaviourPy] 实体 {self.entity.id} Python 脚本已启动")

    def on_update(self, dt: float):
        rot = self.get_component(RotationComponent)
        if rot is not None:
            rot.rotation.y += self.speed * dt
)PY";
            (void)loader->saveFile(samplePyPath, kSamplePy);
        }
    }

    void PybindScriptBackend::setupPyBriEnginePackage() {
#ifndef __ANDROID__
        if (!m_pyEnv || !m_enginePackage) {
            return;
        }

        m_enginePackage->registerStruct<FCT::Vec2>("Vec2");
        m_enginePackage->registerStruct<FCT::Vec3<>>("Vec3");
        m_enginePackage->registerStruct<FCT::Vec4>("Vec4");
        m_enginePackage->registerStruct<ProjectConfig>("ProjectConfig");
        m_enginePackage->registerComponentTuple<AllComponentsList>();

        if (Engine::getInstance()) {
            m_enginePackage->bindDescribedService(*m_pyEnv, "Engine", Engine::getInstance());
            if (true) {
                m_enginePackage->bindDescribedService(*m_pyEnv, "Foundation", Engine::getInstance()->foundation.get());
            }
        }
        m_enginePackage->bindDescribedService(*m_pyEnv, "Runtime", &EngineRuntime::instance());
        m_enginePackage->bindDescribedService(*m_pyEnv, "StateMachine", &EngineRuntime::instance());

        DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
        if (dm) {
            m_enginePackage->bindDescribedService(*m_pyEnv, "DataManager", dm);
        }

        if (ComponentReflection* refl = getActiveReflection()) {
            m_enginePackage->bindDescribedService(*m_pyEnv, "ComponentReflection", refl);
        }

        if (g_engineGlobal.memoryManager) {
            m_enginePackage->bindDescribedService(*m_pyEnv, "MemoryManager", g_engineGlobal.memoryManager);
        }
        if (g_engineGlobal.jobSystem) {
            m_enginePackage->bindDescribedService(*m_pyEnv, "JobSystem", g_engineGlobal.jobSystem);
        }
        if (g_engineGlobal.skeletalAnimationSystem) {
            m_enginePackage->bindDescribedService(*m_pyEnv, "SkeletalAnimation", g_engineGlobal.skeletalAnimationSystem);
        }
        if (g_engineGlobal.networkManager) {
            m_enginePackage->bindDescribedService(*m_pyEnv, "Network", g_engineGlobal.networkManager);
        }
        if (g_engineGlobal.networkSyncSystem) {
            m_enginePackage->bindDescribedService(*m_pyEnv, "NetworkSync", g_engineGlobal.networkSyncSystem);
        }

        if (g_engineGlobal.inputSystem) {
            m_enginePackage->bindDescribedService(*m_pyEnv, "Input", g_engineGlobal.inputSystem);
        }

        m_enginePackage->addPythonStubDeclaration(R"PYI(class EntityHandle(dict):
    id: int
    registry: int

class ScriptBehaviour:
    entity: EntityHandle
    position: Vec3
    rotation: Vec3
    scale: Vec3
    @property
    def delta_time(self) -> float: ...
    @property
    def deltaTime(self) -> float: ...
    def get_component(self, comp_or_name: Any) -> Any: ...
    def getComponent(self, comp_or_name: Any) -> Any: ...
    def set_component(self, comp_or_name: Any, value: dict[str, Any]) -> bool: ...
    def setComponent(self, comp_or_name: Any, value: dict[str, Any]) -> bool: ...
    def has_component(self, comp_or_name: Any) -> bool: ...
    def hasComponent(self, comp_or_name: Any) -> bool: ...
    def add_component(self, comp_or_name: Any, init_value: dict[str, Any] | None = None) -> Any: ...
    def addComponent(self, comp_or_name: Any, init_value: dict[str, Any] | None = None) -> Any: ...
    def remove_component(self, comp_or_name: Any) -> bool: ...
    def removeComponent(self, comp_or_name: Any) -> bool: ...
    def use_component(self, comp_or_name: Any, auto_add: bool = False) -> Any: ...
    def useComponent(self, comp_or_name: Any, auto_add: bool = False) -> Any: ...
    def set_parent(self, parent_entity_id: int, trunk_name: str = 'default') -> bool: ...
    def clear_parent(self, trunk_name: str = 'default') -> bool: ...
    def get_parent(self, trunk_name: str = 'default') -> int: ...
    def get_children(self, trunk_name: str = 'default') -> list[int]: ...
    def get_world_position(self, trunk_name: str = 'default') -> Vec3: ...
    def set_world_position(self, world_pos: Vec3, trunk_name: str = 'default') -> bool: ...
    def look_at(self, target: Vec3, trunk_name: str = 'default') -> bool: ...
    def on_start(self) -> None: ...
    def on_update(self, dt: float) -> None: ...
    def _ready(self) -> None: ...
    def _process(self, delta: float) -> None: ...

Node3D = ScriptBehaviour
Node = ScriptBehaviour

def registerPlugin(plugin_def: dict[str, Any]) -> dict[str, Any]: ...)PYI");

        m_enginePackage->mountToEnvironment(*m_pyEnv, "MQEngine");
#endif
    }

    void PybindScriptBackend::initializeCoreBridge(const std::string& scriptsRoot) {
#ifndef __ANDROID__
        if (!m_pyEnv) {
            return;
        }

        setupPyBriEnginePackage();
        exportTypeDefinitions(scriptsRoot);

        if (m_isCoreBridgeInitialized) {
            m_pyEnv->executeScript(R"PY(
if 'engine' not in globals() or not isinstance(globals()['engine'], dict):
    globals()['engine'] = __PyBriAttrDict({'logicDealt': 0.0})
if 'MQEngine' in globals() and hasattr(MQEngine, 'Engine'):
    for _k, _v in MQEngine.Engine.items():
        globals()['engine'][_k] = _v
)PY");
            return;
        }

        static const char* kCoreBridgeBootstrap = R"PY(
__mqPyPlugins = globals().get('__mqPyPlugins', {})
__mqPyScriptClasses = globals().get('__mqPyScriptClasses', {})
entityInfo = globals().get('entityInfo', __PyBriAttrDict({'registry_ptr': 0, 'entity_id': 0}))
engine = globals().get('engine', __PyBriAttrDict({'logicDealt': 0.0}))

def __mqGetPluginLifecycleNames(plugin_name: str):
    p = __mqPyPlugins.get(plugin_name)
    if p is None:
        return []
    supported = []
    for method_name in ('onLoad', 'onInit', 'onLogicTick', 'onRenderTick', 'onUnload'):
        fn = p.get(method_name) if isinstance(p, dict) else getattr(p, method_name, None)
        if callable(fn):
            supported.append(method_name)
    return supported

def __mqInvokePluginLifecycle(plugin_name: str, method_name: str, dt: float = 0.016):
    p = __mqPyPlugins.get(plugin_name)
    if p is None:
        return
    fn = p.get(method_name) if isinstance(p, dict) else getattr(p, method_name, None)
    if not callable(fn):
        return
    if method_name == 'onLogicTick':
        fn(float(dt))
    else:
        try:
            fn(MQEngine)
        except TypeError:
            fn()

if 'MQEngine' in globals():
    def _mq_register_plugin(plugin_def):
        if isinstance(plugin_def, dict) and 'name' in plugin_def:
            __mqPyPlugins[plugin_def['name']] = plugin_def
        elif hasattr(plugin_def, 'name'):
            __mqPyPlugins[getattr(plugin_def, 'name')] = plugin_def
        return plugin_def
    MQEngine['registerPlugin'] = _mq_register_plugin
    if hasattr(MQEngine, 'Engine') and isinstance(MQEngine.Engine, dict):
        for _k, _v in MQEngine.Engine.items():
            engine[_k] = _v
    __pybriSyncModule('mqengine')
)PY";

        try {
            m_pyEnv->executeScript(kCoreBridgeBootstrap);
            m_pyEnv->tick();
            m_isCoreBridgeInitialized = true;
        } catch (const std::exception& e) {
            spdlog::error("PybindScriptBackend (FCT_PyBri) 初始化核心 Python 桥接失败: {}", e.what());
        }
#else
        (void)scriptsRoot;
#endif
    }

    void PybindScriptBackend::injectEntityRuntimeBootstrap() {
#ifndef __ANDROID__
        if (!m_pyEnv) {
            return;
        }

        static const char* kEntityBootstrap = R"PY(
def __mqPyResolveComponentName(comp_or_name):
    if isinstance(comp_or_name, str):
        return comp_or_name
    if hasattr(comp_or_name, 'componentName'):
        return str(getattr(comp_or_name, 'componentName'))
    if hasattr(comp_or_name, '__name__'):
        return str(getattr(comp_or_name, '__name__'))
    return str(comp_or_name or '')


class __MqReactiveVecProxy(__PyBriAttrDict):
    def __init__(self, get_entity_fn, comp_name, field_name, raw_vec):
        super().__init__()
        object.__setattr__(self, '_get_entity_fn', get_entity_fn)
        object.__setattr__(self, '_comp_name', comp_name)
        object.__setattr__(self, '_field_name', field_name)
        if isinstance(raw_vec, dict):
            for k, v in raw_vec.items():
                dict.__setitem__(self, k, v)

    def __setattr__(self, key, value):
        if key.startswith('_'):
            object.__setattr__(self, key, value)
            return
        dict.__setitem__(self, key, value)
        ent = self._get_entity_fn()
        if ent and ent.get('registry'):
            entityInfo.setComponentFieldById(
                float(ent['registry']),
                int(ent['id']),
                self._comp_name,
                self._field_name,
                dict(self)
            )

    def __setitem__(self, key, value):
        dict.__setitem__(self, key, value)
        ent = self._get_entity_fn()
        if ent and ent.get('registry'):
            entityInfo.setComponentFieldById(
                float(ent['registry']),
                int(ent['id']),
                self._comp_name,
                self._field_name,
                dict(self)
            )


class __MqReactiveComponentProxy(__PyBriAttrDict):
    def __init__(self, get_entity_fn, comp_name, auto_add=False, initial_snapshot=None):
        super().__init__()
        object.__setattr__(self, '_get_entity_fn', get_entity_fn)
        object.__setattr__(self, '_comp_name', comp_name)
        object.__setattr__(self, '_auto_add', bool(auto_add))
        if isinstance(initial_snapshot, dict):
            for k, v in initial_snapshot.items():
                dict.__setitem__(self, k, v)

    def __getattr__(self, key):
        if key.startswith('_'):
            return object.__getattribute__(self, key)
        ent = self._get_entity_fn()
        if not ent or not ent.get('registry'):
            return dict.get(self, key)
        reg = float(ent['registry'])
        eid = int(ent['id'])
        if self._auto_add and not entityInfo.hasComponentById(reg, eid, self._comp_name):
            entityInfo.addComponentById(reg, eid, self._comp_name)
        val = entityInfo.getComponentFieldById(reg, eid, self._comp_name, str(key))
        if val is None:
            return dict.get(self, key)
        dict.__setitem__(self, key, val)
        if isinstance(val, dict):
            return __MqReactiveVecProxy(self._get_entity_fn, self._comp_name, str(key), val)
        return val

    def __setattr__(self, key, value):
        if key.startswith('_'):
            object.__setattr__(self, key, value)
            return
        dict.__setitem__(self, key, value)
        ent = self._get_entity_fn()
        if ent and ent.get('registry'):
            reg = float(ent['registry'])
            eid = int(ent['id'])
            if self._auto_add and not entityInfo.hasComponentById(reg, eid, self._comp_name):
                entityInfo.addComponentById(reg, eid, self._comp_name)
            entityInfo.setComponentFieldById(reg, eid, self._comp_name, str(key), value)


class ScriptBehaviour:
    def __init__(self):
        pending = globals().get('__mqPendingPyEntityContext')
        if isinstance(pending, dict):
            self.entity = __PyBriAttrDict({'id': int(pending.get('id', 0)), 'registry': int(pending.get('registry', 0))})
        else:
            self.entity = __PyBriAttrDict({'id': 0, 'registry': 0})
        self._tickers = {}

    @property
    def delta_time(self) -> float:
        return float(engine.get('logicDealt', 0.016)) if isinstance(engine, dict) else 0.016

    @property
    def deltaTime(self) -> float:
        return self.delta_time

    @property
    def position(self):
        comp = self.get_component('PositionComponent')
        return comp.position if comp is not None else __PyBriAttrDict({'x': 0.0, 'y': 0.0, 'z': 0.0})

    @position.setter
    def position(self, val):
        if val is not None and self.entity and self.entity.get('registry'):
            entityInfo.setComponentFieldById(float(self.entity['registry']), int(self.entity['id']), 'PositionComponent', 'position', val)

    @property
    def rotation(self):
        comp = self.get_component('RotationComponent')
        return comp.rotation if comp is not None else __PyBriAttrDict({'x': 0.0, 'y': 0.0, 'z': 0.0})

    @rotation.setter
    def rotation(self, val):
        if val is not None and self.entity and self.entity.get('registry'):
            entityInfo.setComponentFieldById(float(self.entity['registry']), int(self.entity['id']), 'RotationComponent', 'rotation', val)

    @property
    def rotation_degrees(self):
        return self.rotation

    @rotation_degrees.setter
    def rotation_degrees(self, val):
        self.rotation = val

    @property
    def scale(self):
        comp = self.get_component('ScaleComponent')
        return comp.scale if comp is not None else __PyBriAttrDict({'x': 1.0, 'y': 1.0, 'z': 1.0})

    @scale.setter
    def scale(self, val):
        if val is not None and self.entity and self.entity.get('registry'):
            entityInfo.setComponentFieldById(float(self.entity['registry']), int(self.entity['id']), 'ScaleComponent', 'scale', val)

    def get_component(self, comp_or_name):
        comp_name = __mqPyResolveComponentName(comp_or_name)
        if not comp_name or not self.entity or not self.entity.get('registry'):
            return None
        raw_obj = entityInfo.getComponentById(float(self.entity['registry']), int(self.entity['id']), comp_name)
        if raw_obj is None:
            return None
        return __MqReactiveComponentProxy(lambda: self.entity, comp_name, False, raw_obj)

    getComponent = get_component

    def set_component(self, comp_or_name, value):
        comp_name = __mqPyResolveComponentName(comp_or_name)
        if not comp_name or not self.entity or not self.entity.get('registry'):
            return False
        return bool(entityInfo.setComponentById(float(self.entity['registry']), int(self.entity['id']), comp_name, value))

    setComponent = set_component

    def has_component(self, comp_or_name):
        comp_name = __mqPyResolveComponentName(comp_or_name)
        if not comp_name or not self.entity or not self.entity.get('registry'):
            return False
        return bool(entityInfo.hasComponentById(float(self.entity['registry']), int(self.entity['id']), comp_name))

    hasComponent = has_component

    def add_component(self, comp_or_name, init_value=None):
        comp_name = __mqPyResolveComponentName(comp_or_name)
        if not comp_name or not self.entity or not self.entity.get('registry'):
            return None
        entityInfo.addComponentById(float(self.entity['registry']), int(self.entity['id']), comp_name)
        if isinstance(init_value, dict):
            entityInfo.setComponentById(float(self.entity['registry']), int(self.entity['id']), comp_name, init_value)
        return self.get_component(comp_name)

    addComponent = add_component

    def remove_component(self, comp_or_name):
        comp_name = __mqPyResolveComponentName(comp_or_name)
        if not comp_name or not self.entity or not self.entity.get('registry'):
            return False
        return bool(entityInfo.removeComponentById(float(self.entity['registry']), int(self.entity['id']), comp_name))

    removeComponent = remove_component

    def use_component(self, comp_or_name, auto_add=False):
        comp_name = __mqPyResolveComponentName(comp_or_name)
        return __MqReactiveComponentProxy(lambda: self.entity, comp_name, bool(auto_add), {})

    useComponent = use_component

    def set_parent(self, parent_entity_id: int, trunk_name: str = 'default') -> bool:
        if 'MQEngine' not in globals() or not hasattr(MQEngine, 'DataManager'):
            return False
        return bool(MQEngine.DataManager.setParent(trunk_name, int(self.entity['id']), int(parent_entity_id)))

    setParent = set_parent

    def clear_parent(self, trunk_name: str = 'default') -> bool:
        if 'MQEngine' not in globals() or not hasattr(MQEngine, 'DataManager'):
            return False
        return bool(MQEngine.DataManager.clearParent(trunk_name, int(self.entity['id'])))

    clearParent = clear_parent

    def get_parent(self, trunk_name: str = 'default') -> int:
        if 'MQEngine' not in globals() or not hasattr(MQEngine, 'DataManager'):
            return 0xFFFFFFFF
        return int(MQEngine.DataManager.getParent(trunk_name, int(self.entity['id'])))

    getParent = get_parent

    def get_children(self, trunk_name: str = 'default'):
        if 'MQEngine' not in globals() or not hasattr(MQEngine, 'DataManager'):
            return []
        return MQEngine.DataManager.getChildren(trunk_name, int(self.entity['id']))

    getChildren = get_children

    def get_world_position(self, trunk_name: str = 'default'):
        if 'MQEngine' not in globals() or not hasattr(MQEngine, 'DataManager'):
            return self.position
        return MQEngine.DataManager.getWorldPosition(trunk_name, int(self.entity['id']))

    getWorldPosition = get_world_position

    def set_world_position(self, world_pos, trunk_name: str = 'default') -> bool:
        if 'MQEngine' not in globals() or not hasattr(MQEngine, 'DataManager') or world_pos is None:
            return False
        return bool(MQEngine.DataManager.setWorldPosition(trunk_name, int(self.entity['id']), world_pos))

    setWorldPosition = set_world_position

    def look_at(self, target, trunk_name: str = 'default') -> bool:
        if 'MQEngine' not in globals() or not hasattr(MQEngine, 'DataManager') or target is None:
            return False
        return bool(MQEngine.DataManager.lookAt(trunk_name, int(self.entity['id']), target))

    lookAt = look_at

    def rotate_y(self, angle_rad: float):
        import math
        rot = self.get_component('RotationComponent')
        if rot is not None:
            rot.rotation.y += math.degrees(angle_rad)

    def rotate_x(self, angle_rad: float):
        import math
        rot = self.get_component('RotationComponent')
        if rot is not None:
            rot.rotation.x += math.degrees(angle_rad)

    def rotate_z(self, angle_rad: float):
        import math
        rot = self.get_component('RotationComponent')
        if rot is not None:
            rot.rotation.z += math.degrees(angle_rad)

    def translate(self, offset):
        pos = self.get_component('PositionComponent')
        if pos is not None and isinstance(offset, dict):
            pos.position.x += float(offset.get('x', 0.0))
            pos.position.y += float(offset.get('y', 0.0))
            pos.position.z += float(offset.get('z', 0.0))

    def _onTicker(self):
        for fn in list(getattr(self, '_tickers', {}).values()):
            if callable(fn):
                fn()


Node3D = ScriptBehaviour
Node = ScriptBehaviour
MeshInstance3D = ScriptBehaviour
CharacterBody3D = ScriptBehaviour
RigidBody3D = ScriptBehaviour
Area3D = ScriptBehaviour
Camera3D = ScriptBehaviour
DirectionalLight3D = ScriptBehaviour
OmniLight3D = ScriptBehaviour

import math as _mq_math
PI = _mq_math.pi
TAU = _mq_math.tau
sin = _mq_math.sin
cos = _mq_math.cos
tan = _mq_math.tan
sqrt = _mq_math.sqrt
deg_to_rad = _mq_math.radians
rad_to_deg = _mq_math.degrees

def lerp(a, b, t: float):
    return a + (b - a) * float(t)

def clamp(val, min_val, max_val):
    return max(min_val, min(max_val, val))

def Vector2(x: float = 0.0, y: float = 0.0):
    return __PyBriAttrDict({'x': float(x), 'y': float(y)})

def Vector3(x: float = 0.0, y: float = 0.0, z: float = 0.0):
    return __PyBriAttrDict({'x': float(x), 'y': float(y), 'z': float(z)})

def Vector4(x: float = 0.0, y: float = 0.0, z: float = 0.0, w: float = 0.0):
    return __PyBriAttrDict({'x': float(x), 'y': float(y), 'z': float(z), 'w': float(w)})

def Color(r: float = 1.0, g: float = 1.0, b: float = 1.0, a: float = 1.0):
    return __PyBriAttrDict({'x': float(r), 'y': float(g), 'z': float(b), 'w': float(a)})

if 'MQEngine' in globals() and isinstance(MQEngine, dict):
    MQEngine['ScriptBehaviour'] = ScriptBehaviour
    MQEngine['Node3D'] = Node3D
    MQEngine['Node'] = Node
    MQEngine['Vector2'] = Vector2
    MQEngine['Vector3'] = Vector3
    MQEngine['Vector4'] = Vector4
    MQEngine['Color'] = Color
    MQEngine['deg_to_rad'] = deg_to_rad
    MQEngine['rad_to_deg'] = rad_to_deg
    MQEngine['lerp'] = lerp
    MQEngine['clamp'] = clamp
    __pybriSyncModule('mqengine')


def createPythonEntityObject(entity_id: int, registry_ptr: int):
    obj = ScriptBehaviour()
    obj.entity = __PyBriAttrDict({'id': int(entity_id), 'registry': int(registry_ptr)})
    return obj


def assignPythonGlobalFunction(obj, func_name: str):
    fn = globals().get(func_name)
    if callable(fn) and obj is not None:
        if not hasattr(obj, '_tickers') or not isinstance(obj._tickers, dict):
            obj._tickers = {}
        obj._tickers[func_name] = fn


def createPythonScriptInstance(class_name: str, entity_id: int, registry_ptr: int):
    cls = __mqPyScriptClasses.get(class_name) or globals().get(class_name)
    if isinstance(cls, type):
        prev_pending = globals().get('__mqPendingPyEntityContext')
        globals()['__mqPendingPyEntityContext'] = {'id': int(entity_id), 'registry': int(registry_ptr)}
        try:
            inst = cls()
            inst.entity = __PyBriAttrDict({'id': int(entity_id), 'registry': int(registry_ptr)})
            if not hasattr(inst, '_tickers'):
                inst._tickers = {}
            return inst
        finally:
            globals()['__mqPendingPyEntityContext'] = prev_pending
    return None


def invokePythonEntityStart(obj):
    if obj is None:
        return
    for method_name in ('onStart', 'on_start', '_ready'):
        fn = getattr(obj, method_name, None)
        if callable(fn):
            fn()


def invokePythonEntityUpdate(obj, dt: float):
    if obj is None:
        return
    for method_name in ('onUpdate', 'on_update', '_process', '_physics_process'):
        fn = getattr(obj, method_name, None)
        if callable(fn):
            fn(dt)
    for ticker_name in ('onTicker', 'on_ticker', '_onTicker'):
        fn = getattr(obj, ticker_name, None)
        if callable(fn):
            fn()


def getGlobalPythonClassNames():
    ignored = {
        '__PyBriAttrDict', '__MqReactiveVecProxy', '__MqReactiveComponentProxy',
        'ScriptBehaviour', 'Node3D', 'Node', 'MeshInstance3D', 'CharacterBody3D',
        'RigidBody3D', 'Area3D', 'Camera3D', 'DirectionalLight3D', 'OmniLight3D'
    }
    result = set(__mqPyScriptClasses.keys())
    for name, val in list(globals().items()):
        if name.startswith('_') or name in ignored:
            continue
        if isinstance(val, type):
            if getattr(val, '__isMqComponentClass', False):
                continue
            if issubclass(val, ScriptBehaviour) and val is not ScriptBehaviour:
                result.add(name)
    return sorted(list(result))
)PY";

        m_pyEnv->executeScript(kEntityBootstrap);
#endif
    }

    void PybindScriptBackend::registerEntityFunctions() {
#ifndef __ANDROID__
        if (!m_pyEnv) {
            return;
        }
        FCT::PyBri::Object globals = m_pyEnv->global();
        FCT::PyBri::Object entityInfo = globals.getItemObject("entityInfo");
        if (!entityInfo.isValid()) {
            entityInfo = m_pyEnv->createObject();
            globals.setItemObject("entityInfo", entityInfo);
        }

        FCT::PyBri::bindFunctionToObject(*m_pyEnv, entityInfo, "getComponentById",
            [this](double registryPtrDouble, uint32_t entityId, std::string componentName) -> FCT::PyBri::Object {
                try {
                    ComponentReflection* refl = getActiveReflection();
                    DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                    if (!refl || !dm) return FCT::PyBri::Object();

                    uint64_t registryPtr = static_cast<uint64_t>(registryPtrDouble);
                    auto registries = dm->currentRegistries();
                    auto* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                    if (std::find(registries.begin(), registries.end(), targetRegistry) != registries.end()) {
                        entt::entity entity = static_cast<entt::entity>(entityId);
                        if (!refl->hasComponent(*targetRegistry, entity, componentName)) {
                            return FCT::PyBri::Object();
                        }
                        FCT::PyBri::Object pyObj = m_pyEnv->createObject();
                        for (const auto& fieldName : refl->getComponentFieldNames(componentName)) {
                            try {
                                auto value = refl->getComponentField(*targetRegistry, entity, componentName, fieldName);
                                pyObj.setItemObject(fieldName, convertComponentValueToPy(value));
                            } catch (const std::exception&) {
                            }
                        }
                        return pyObj;
                    }
                } catch (const std::exception&) {
                }
                return FCT::PyBri::Object();
            });

        FCT::PyBri::bindFunctionToObject(*m_pyEnv, entityInfo, "setComponentById",
            [this](double registryPtrDouble, uint32_t entityId, std::string componentName, FCT::PyBri::Object componentObj) -> bool {
                try {
                    ComponentReflection* refl = getActiveReflection();
                    DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                    if (!refl || !dm) return false;

                    uint64_t registryPtr = static_cast<uint64_t>(registryPtrDouble);
                    auto registries = dm->currentRegistries();
                    auto* targetRegistry = reinterpret_cast<entt::registry*>(registryPtr);
                    if (std::find(registries.begin(), registries.end(), targetRegistry) != registries.end()) {
                        entt::entity entity = static_cast<entt::entity>(entityId);
                        if (!refl->hasComponent(*targetRegistry, entity, componentName)) {
                            refl->addComponent(*targetRegistry, entity, componentName);
                        }
                        for (const auto& fieldName : refl->getComponentFieldNames(componentName)) {
                            if (componentObj.hasProperty(fieldName)) {
                                std::string fieldType = refl->getComponentFieldType(componentName, fieldName);
                                try {
                                    FCT::PyBri::Object fieldVal = componentObj.getItemObject(fieldName);
                                    ComponentValue compVal = convertPyObjectToComponentValue(fieldType, fieldVal, fieldName);
                                    refl->setComponentField(*targetRegistry, entity, componentName, fieldName, compVal);
                                } catch (const std::exception&) {
                                }
                            }
                        }
                        return true;
                    }
                } catch (const std::exception&) {
                }
                return false;
            });

        FCT::PyBri::bindFunctionToObject(*m_pyEnv, entityInfo, "hasComponentById",
            [this](double registryPtrDouble, uint32_t entityId, std::string componentName) -> bool {
                try {
                    ComponentReflection* refl = getActiveReflection();
                    DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                    if (!refl || !dm) return false;
                    auto* targetRegistry = reinterpret_cast<entt::registry*>(static_cast<uint64_t>(registryPtrDouble));
                    auto registries = dm->currentRegistries();
                    if (std::find(registries.begin(), registries.end(), targetRegistry) != registries.end()) {
                        return refl->hasComponent(*targetRegistry, static_cast<entt::entity>(entityId), componentName);
                    }
                } catch (const std::exception&) {}
                return false;
            });

        FCT::PyBri::bindFunctionToObject(*m_pyEnv, entityInfo, "addComponentById",
            [this](double registryPtrDouble, uint32_t entityId, std::string componentName) -> bool {
                try {
                    ComponentReflection* refl = getActiveReflection();
                    DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                    if (!refl || !dm) return false;
                    auto* targetRegistry = reinterpret_cast<entt::registry*>(static_cast<uint64_t>(registryPtrDouble));
                    auto registries = dm->currentRegistries();
                    if (std::find(registries.begin(), registries.end(), targetRegistry) != registries.end()) {
                        refl->addComponent(*targetRegistry, static_cast<entt::entity>(entityId), componentName);
                        return true;
                    }
                } catch (const std::exception&) {}
                return false;
            });

        FCT::PyBri::bindFunctionToObject(*m_pyEnv, entityInfo, "removeComponentById",
            [this](double registryPtrDouble, uint32_t entityId, std::string componentName) -> bool {
                try {
                    ComponentReflection* refl = getActiveReflection();
                    DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                    if (!refl || !dm) return false;
                    auto* targetRegistry = reinterpret_cast<entt::registry*>(static_cast<uint64_t>(registryPtrDouble));
                    auto registries = dm->currentRegistries();
                    if (std::find(registries.begin(), registries.end(), targetRegistry) != registries.end()) {
                        refl->removeComponent(*targetRegistry, static_cast<entt::entity>(entityId), componentName);
                        return true;
                    }
                } catch (const std::exception&) {}
                return false;
            });

        FCT::PyBri::bindFunctionToObject(*m_pyEnv, entityInfo, "getComponentFieldById",
            [this](double registryPtrDouble, uint32_t entityId, std::string componentName, std::string fieldName) -> FCT::PyBri::Object {
                try {
                    ComponentReflection* refl = getActiveReflection();
                    DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                    if (!refl || !dm) return FCT::PyBri::Object();
                    auto* targetRegistry = reinterpret_cast<entt::registry*>(static_cast<uint64_t>(registryPtrDouble));
                    auto registries = dm->currentRegistries();
                    if (std::find(registries.begin(), registries.end(), targetRegistry) != registries.end()) {
                        entt::entity entity = static_cast<entt::entity>(entityId);
                        if (!refl->hasComponent(*targetRegistry, entity, componentName)) {
                            return FCT::PyBri::Object();
                        }
                        auto value = refl->getComponentField(*targetRegistry, entity, componentName, fieldName);
                        return convertComponentValueToPy(value);
                    }
                } catch (const std::exception&) {}
                return FCT::PyBri::Object();
            });

        FCT::PyBri::bindFunctionToObject(*m_pyEnv, entityInfo, "setComponentFieldById",
            [this](double registryPtrDouble, uint32_t entityId, std::string componentName, std::string fieldName, FCT::PyBri::Object fieldValue) -> bool {
                try {
                    ComponentReflection* refl = getActiveReflection();
                    DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
                    if (!refl || !dm) return false;
                    auto* targetRegistry = reinterpret_cast<entt::registry*>(static_cast<uint64_t>(registryPtrDouble));
                    auto registries = dm->currentRegistries();
                    if (std::find(registries.begin(), registries.end(), targetRegistry) != registries.end()) {
                        entt::entity entity = static_cast<entt::entity>(entityId);
                        if (!refl->hasComponent(*targetRegistry, entity, componentName)) {
                            refl->addComponent(*targetRegistry, entity, componentName);
                        }
                        std::string fieldType = refl->getComponentFieldType(componentName, fieldName);
                        if (fieldType.empty()) return false;
                        ComponentValue compVal = convertPyObjectToComponentValue(fieldType, fieldValue, fieldName);
                        refl->setComponentField(*targetRegistry, entity, componentName, fieldName, compVal);
                        return true;
                    }
                } catch (const std::exception&) {}
                return false;
            });
#endif
    }

    void PybindScriptBackend::registerInputFunctions() {
#ifndef __ANDROID__
        if (!m_pyEnv) {
            return;
        }
        FCT::PyBri::Object globals = m_pyEnv->global();
        FCT::PyBri::Object inputObj = m_pyEnv->createObject();

        FCT::PyBri::bindFunctionToObject(*m_pyEnv, inputObj, "isKeyPressed", [](int key) -> bool {
            return g_engineGlobal.inputSystem ? g_engineGlobal.inputSystem->isKeyPressed(key) : false;
        });
        FCT::PyBri::bindFunctionToObject(*m_pyEnv, inputObj, "isMouseButtonPressed", [](int btn) -> bool {
            return g_engineGlobal.inputSystem ? g_engineGlobal.inputSystem->isMouseButtonPressed(btn) : false;
        });
        FCT::PyBri::bindFunctionToObject(*m_pyEnv, inputObj, "isLeftMousePressed", []() -> bool {
            return g_engineGlobal.inputSystem ? g_engineGlobal.inputSystem->isLeftMousePressed() : false;
        });
        FCT::PyBri::bindFunctionToObject(*m_pyEnv, inputObj, "isRightMousePressed", []() -> bool {
            return g_engineGlobal.inputSystem ? g_engineGlobal.inputSystem->isRightMousePressed() : false;
        });
        FCT::PyBri::bindFunctionToObject(*m_pyEnv, inputObj, "getMousePosition", []() -> FCT::Vec2 {
            return g_engineGlobal.inputSystem ? g_engineGlobal.inputSystem->getMousePosition() : FCT::Vec2(0.0f, 0.0f);
        });
        FCT::PyBri::bindFunctionToObject(*m_pyEnv, inputObj, "getMouseDelta", []() -> FCT::Vec2 {
            return g_engineGlobal.inputSystem ? g_engineGlobal.inputSystem->getMouseDelta() : FCT::Vec2(0.0f, 0.0f);
        });
        FCT::PyBri::bindFunctionToObject(*m_pyEnv, inputObj, "getMouseWheelDelta", []() -> int {
            return g_engineGlobal.inputSystem ? g_engineGlobal.inputSystem->getMouseWheelDelta() : 0;
        });

        globals.setItemObject("Input", inputObj);
#endif
    }

    void PybindScriptBackend::executeSinglePythonFile(const std::string& filePath, const std::string& rawCode) {
#ifndef __ANDROID__
        if (!m_pyEnv) {
            return;
        }
        try {
            m_pyEnv->executeFile(SAI::Path::normalize(filePath), rawCode);
            m_pyEnv->executeScript(R"PY(
for _k, _v in list(globals().items()):
    if not _k.startswith('_') and isinstance(_v, type):
        if not getattr(_v, '__isMqComponentClass', False) and _v is not ScriptBehaviour:
            if issubclass(_v, ScriptBehaviour):
                __mqPyScriptClasses[_k] = _v
)PY");
        } catch (const std::exception& e) {
            spdlog::error("执行 Python 脚本 [{}] 失败: {}", filePath, e.what());
        }
#else
        (void)filePath;
        (void)rawCode;
#endif
    }

    void PybindScriptBackend::setLogicDeltaTime(float deltaTime) {
        m_logicDeltaTime = deltaTime;
#ifndef __ANDROID__
        if (m_pyEnv && m_isAvailable) {
            try {
                FCT::PyBri::Object engineObj = m_pyEnv->global().getItemObject("engine");
                if (engineObj.isValid()) {
                    engineObj.set("logicDealt", deltaTime);
                }
            } catch (const std::exception&) {
            }
        }
#endif
    }

    void PybindScriptBackend::tick() {
#ifndef __ANDROID__
        if (m_pyEnv && m_isAvailable) {
            try {
                m_pyEnv->tick();
            } catch (const std::exception&) {
            }
        }
#endif
    }

    std::vector<std::string> PybindScriptBackend::getFunctionNames() const {
#ifndef __ANDROID__
        if (!m_pyEnv || !m_isAvailable) {
            return {};
        }
        try {
            return m_pyEnv->getGlobalFunctionNames();
        } catch (const std::exception&) {
            return {};
        }
#else
        return {};
#endif
    }

    std::vector<std::string> PybindScriptBackend::getClassNames() const {
#ifndef __ANDROID__
        if (!m_pyEnv || !m_isAvailable) {
            return {};
        }
        try {
            return const_cast<FCT::PyBri::PyEnvironment*>(m_pyEnv.get())
                ->callFunction<std::vector<std::string>>("getGlobalPythonClassNames");
        } catch (const std::exception&) {
            return {};
        }
#else
        return {};
#endif
    }

    bool PybindScriptBackend::hasFunction(const std::string& funcName) const {
#ifndef __ANDROID__
        if (!m_pyEnv || !m_isAvailable || funcName.empty()) {
            return false;
        }
        FCT::PyBri::Object fn = m_pyEnv->global().getAttrObject(funcName);
        return fn.isValid() && fn.isCallable();
#else
        (void)funcName;
        return false;
#endif
    }

    bool PybindScriptBackend::hasClass(const std::string& className) const {
#ifndef __ANDROID__
        if (!m_pyEnv || !m_isAvailable || className.empty()) {
            return false;
        }
        auto classes = getClassNames();
        return std::find(classes.begin(), classes.end(), className) != classes.end();
#else
        (void)className;
        return false;
#endif
    }

    Status PybindScriptBackend::executeGlobalEntityFunction(const std::string& funcName,
                                                            uint64_t registryPtr,
                                                            uint32_t entityId) {
#ifndef __ANDROID__
        if (!m_pyEnv || !m_isAvailable) {
            return FailedPreconditionError("PyEnvironment 未就绪");
        }
        try {
            FCT::PyBri::Object entityInfo = m_pyEnv->global().getItemObject("entityInfo");
            if (entityInfo.isValid()) {
                entityInfo.set("registry_ptr", registryPtr);
                entityInfo.set("entity_id", entityId);
            }
            m_pyEnv->callFunction<void>(funcName);
            return OkStatus();
        } catch (const std::exception& e) {
            return InternalError(StrCat("执行 Python 函数 [", funcName, "] 失败: ", e.what()));
        }
#else
        (void)funcName;
        (void)registryPtr;
        (void)entityId;
        return UnimplementedError("Android 暂未启用 Python 后端");
#endif
    }

    Status PybindScriptBackend::executeModuleScript(const std::string& moduleName,
                                                    const std::string& moduleVersion,
                                                    const std::string& entryPath,
                                                    const std::string& moduleDir,
                                                    ResourceLoader* resourceLoader) {
#ifndef __ANDROID__
        if (!m_pyEnv || !m_isAvailable) {
            return FailedPreconditionError("FCT_PyBri 运行时未初始化");
        }

        initializeCoreBridge("./Assets/Scripts");
        m_pyEnv->addModulePath(moduleDir);

        UniquePtr<DiskResourceLoader> fallbackLoader;
        if (!resourceLoader) {
            fallbackLoader = makeUnique<DiskResourceLoader>();
            resourceLoader = fallbackLoader.get();
        }

        auto scriptRes = resourceLoader->loadFile(entryPath);
        CHECK_STATUS(scriptRes);

        std::string escapedName = EscapePythonSingleQuotedString(moduleName);
        std::string escapedVersion = EscapePythonSingleQuotedString(moduleVersion);
        std::string escapedFile = EscapePythonSingleQuotedString(SAI::Path::normalize(entryPath));
        std::string escapedDir = EscapePythonSingleQuotedString(SAI::Path::normalize(moduleDir));

        try {
            m_pyEnv->executeScript(StrCat(
                "__file__ = '", escapedFile, "'\n"
                "__plugin_dir__ = '", escapedDir, "'\n"
            ));
            m_pyEnv->executeFile(entryPath, scriptRes.value());

            std::string registerCheck = StrCat(
                "if '", escapedName, "' not in __mqPyPlugins:\n"
                "    _cand = globals().get('plugin') or globals().get('", escapedName, "')\n"
                "    if isinstance(_cand, type):\n"
                "        _cand = _cand()\n"
                "    if _cand is not None:\n"
                "        __mqPyPlugins['", escapedName, "'] = _cand\n"
            );
            m_pyEnv->executeScript(registerCheck);
            cacheModuleLifecycleMethods(moduleName);
            m_pyEnv->tick();
        } catch (const std::exception& e) {
            return InternalError(StrCat("执行 Python 插件脚本 [", moduleName, "] 失败: ", e.what()));
        }
        return OkStatus();
#else
        (void)moduleName;
        (void)moduleVersion;
        (void)entryPath;
        (void)moduleDir;
        (void)resourceLoader;
        return UnimplementedError("FCT_PyBri 暂未启用");
#endif
    }

    void PybindScriptBackend::cacheModuleLifecycleMethods(const std::string& moduleName) {
#ifndef __ANDROID__
        if (!m_pyEnv || !m_isAvailable) {
            return;
        }
        std::unordered_set<std::string> supportedSet;
        try {
            auto names = m_pyEnv->callFunction<std::vector<std::string>>("__mqGetPluginLifecycleNames", moduleName);
            supportedSet.insert(names.begin(), names.end());
        } catch (const std::exception&) {
        }
        std::lock_guard<std::mutex> lock(m_lifecycleCacheMutex);
        m_moduleLifecycleMethods[moduleName] = std::move(supportedSet);
#else
        (void)moduleName;
#endif
    }

    bool PybindScriptBackend::hasCachedModuleLifecycleMethod(const std::string& moduleName,
                                                             const std::string& methodName) const {
        std::lock_guard<std::mutex> lock(m_lifecycleCacheMutex);
        auto it = m_moduleLifecycleMethods.find(moduleName);
        if (it == m_moduleLifecycleMethods.end()) {
            return true;
        }
        return it->second.find(methodName) != it->second.end();
    }

    Status PybindScriptBackend::invokeModuleLifecycle(const std::string& moduleName,
                                                      const std::string& methodName,
                                                      const std::string& argsJson) {
#ifndef __ANDROID__
        if (!m_pyEnv || !m_isAvailable) {
            return FailedPreconditionError("FCT_PyBri 运行时未初始化");
        }

        if (!hasCachedModuleLifecycleMethod(moduleName, methodName)) {
            return OkStatus();
        }

        float dt = (methodName == "onLogicTick")
            ? ParseDeltaTimeFromArgsJson(argsJson, m_logicDeltaTime)
            : m_logicDeltaTime;

        try {
            m_pyEnv->callFunction<void>("__mqInvokePluginLifecycle", moduleName, methodName, dt);
        } catch (const std::exception& e) {
            return InternalError(StrCat("调用 Python 插件 [", moduleName, "::", methodName, "] 异常: ", e.what()));
        }
        return OkStatus();
#else
        (void)moduleName;
        (void)methodName;
        (void)argsJson;
        return UnimplementedError("FCT_PyBri 暂未启用");
#endif
    }

    void PybindScriptBackend::unloadModuleScript(const std::string& moduleName) {
#ifndef __ANDROID__
        if (!m_pyEnv || !m_isAvailable) {
            return;
        }
        (void)invokeModuleLifecycle(moduleName, "onUnload");
        {
            std::lock_guard<std::mutex> lock(m_lifecycleCacheMutex);
            m_moduleLifecycleMethods.erase(moduleName);
        }
        try {
            std::string escapedName = EscapePythonSingleQuotedString(moduleName);
            m_pyEnv->executeScript("if '__mqPyPlugins' in globals(): __mqPyPlugins.pop('" + escapedName + "', None)");
            m_pyEnv->tick();
        } catch (...) {
        }
#else
        (void)moduleName;
#endif
    }

    std::vector<std::string> PybindScriptBackend::loadScriptFilesFromDirectory(const std::string& directory, const std::string& ext) {
        std::vector<std::string> scriptFiles;
        DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
        if (!dm || !dm->getDataLoader()) {
            return scriptFiles;
        }
        DataLoader* dataLoader = dm->getDataLoader();
        try {
            dataLoader->ensureDirectory(directory);
            if (dataLoader->directoryExists(directory)) {
                scriptFiles = dataLoader->getFilePathsWithExtension(directory, ext);
            }
        } catch (const std::exception&) {
        }
        return scriptFiles;
    }

    std::string PybindScriptBackend::readFileContent(const std::string& filePath) {
        ResourceLoader* loader = g_engineGlobal.resourceLoader;
        UniquePtr<DiskResourceLoader> fallbackLoader;
        if (!loader) {
            fallbackLoader = makeUnique<DiskResourceLoader>();
            loader = fallbackLoader.get();
        }
        auto res = loader->loadFile(filePath);
        return res.ok() ? res.value() : "";
    }

#ifndef __ANDROID__
    ComponentValue PybindScriptBackend::convertPyObjectToComponentValue(const std::string& fieldType,
                                                                        const FCT::PyBri::Object& pyObj,
                                                                        const std::string& /*fieldName*/) {
        ::PyObject* rawVal = pyObj.raw();
        if (fieldType == "int") {
            return FCT::PyBri::fromPy<int>(*m_pyEnv, rawVal);
        } else if (fieldType == "float") {
            return FCT::PyBri::fromPy<float>(*m_pyEnv, rawVal);
        } else if (fieldType == "double") {
            return FCT::PyBri::fromPy<double>(*m_pyEnv, rawVal);
        } else if (fieldType == "bool") {
            return FCT::PyBri::fromPy<bool>(*m_pyEnv, rawVal);
        } else if (fieldType == "std::string" || fieldType == "string") {
            return FCT::PyBri::fromPy<std::string>(*m_pyEnv, rawVal);
        } else if (fieldType == "Vec2") {
            return FCT::PyBri::fromPy<FCT::Vec2>(*m_pyEnv, rawVal);
        } else if (fieldType == "Vec3") {
            return FCT::PyBri::fromPy<FCT::Vec3<>>(*m_pyEnv, rawVal);
        } else if (fieldType == "Vec4") {
            return FCT::PyBri::fromPy<FCT::Vec4>(*m_pyEnv, rawVal);
        } else if (fieldType == "string[]") {
            return FCT::PyBri::fromPy<std::vector<std::string>>(*m_pyEnv, rawVal);
        }
        throw std::runtime_error("Unsupported Python component field type: " + fieldType);
    }

    FCT::PyBri::Object PybindScriptBackend::convertComponentValueToPy(const ComponentValue& value) {
        return std::visit([this](const auto& v) -> FCT::PyBri::Object {
            return FCT::PyBri::toPy(*m_pyEnv, v);
        }, value);
    }
#endif

} // namespace MQEngine
