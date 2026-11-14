/**
 * @file GamePlaySubsystem.cpp
 * @brief 游戏业务层实现：状态机驱动、实体挂载式原生脚本与行为组件生命周期编排、动态库加载与定时器调度
 */
#include <MQEngine/domain/gameplay/GamePlaySubsystem.h>
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/host/render/RenderHost.h>
#include <MQEngine/resource/core/DiskResourceLoader.h>
#include <MQEngine/resource/core/GpuMemoryManager.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/runtime/core/Engine.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <unordered_set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#endif

namespace MQEngine
{
    namespace Domain
    {
        namespace GamePlay
        {
            namespace
            {
                inline constexpr const char* kRegisterNativeScriptsSymbol = "RegisterMQNativeScripts";

                std::string normalizeScriptLibraryPath(const std::string& rawPath)
                {
                    if (rawPath.empty()) {
                        return "";
                    }
                    std::error_code ec;
                    std::string norm = std::filesystem::absolute(rawPath, ec).lexically_normal().generic_string();
                    while (norm.size() > 3 && norm.back() == '/') {
                        norm.pop_back();
                    }
                    return norm;
                }

                bool isSharedLibraryEntry(const ResourceFile* entry)
                {
                    if (!entry || !SAI::FileSystem::isFile(entry->path())) {
                        return false;
                    }
                    std::string ext = entry->extension();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                        return static_cast<char>(std::tolower(ch));
                    });
#ifdef _WIN32
                    return ext == ".dll";
#else
                    return ext == ".so";
#endif
                }

                ResourceLoader* resolveGameplayResourceLoader() noexcept
                {
                    if (auto* res = Resource::Resource::InstancePtr()) {
                        if (auto* loader = res->getResourceLoader()) {
                            return loader;
                        }
                    }
                    static DiskResourceLoader s_fallbackLoader;
                    return &s_fallbackLoader;
                }

#ifdef _WIN32
                /**
                 * @brief 只读镜像映射探测 PE 导出表，避免对非脚本 DLL 执行 DllMain 触发 Loader Lock
                 */
                bool peHasExportSymbol(const std::string& utf8Path, const char* symbolName) noexcept
                {
                    if (utf8Path.empty() || !symbolName || symbolName[0] == '\0') {
                        return false;
                    }
                    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, nullptr, 0);
                    if (wideLen <= 1) {
                        return false;
                    }
                    std::wstring widePath(static_cast<size_t>(wideLen - 1), L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, widePath.data(), wideLen);

                    HMODULE imageMod = LoadLibraryExW(
                        widePath.c_str(),
                        nullptr,
                        LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
                    if (!imageMod) {
                        return false;
                    }

                    struct ImageGuard
                    {
                        HMODULE mod = nullptr;
                        ~ImageGuard()
                        {
                            if (mod) {
                                FreeLibrary(mod);
                            }
                        }
                    } guard{imageMod};

                    const auto rawAddr = reinterpret_cast<uintptr_t>(imageMod);
                    if ((rawAddr & static_cast<uintptr_t>(0x2)) == 0) {
                        return false;
                    }
                    const auto* base = reinterpret_cast<const uint8_t*>(rawAddr & ~static_cast<uintptr_t>(0x3));
                    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
                    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
                        return false;
                    }
                    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
                    if (nt->Signature != IMAGE_NT_SIGNATURE ||
                        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC ||
                        nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
                        return false;
                    }

                    const uint32_t imageSize = nt->OptionalHeader.SizeOfImage;
                    const IMAGE_DATA_DIRECTORY exportDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
                    if (exportDir.VirtualAddress == 0 ||
                        exportDir.Size < sizeof(IMAGE_EXPORT_DIRECTORY) ||
                        exportDir.VirtualAddress + sizeof(IMAGE_EXPORT_DIRECTORY) > imageSize) {
                        return false;
                    }

                    const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + exportDir.VirtualAddress);
                    if (exports->NumberOfNames == 0 ||
                        exports->AddressOfNames == 0 ||
                        exports->AddressOfNames + exports->NumberOfNames * sizeof(uint32_t) > imageSize) {
                        return false;
                    }

                    const auto* nameRvas = reinterpret_cast<const uint32_t*>(base + exports->AddressOfNames);
                    for (uint32_t i = 0; i < exports->NumberOfNames; ++i) {
                        const uint32_t nameRva = nameRvas[i];
                        if (nameRva == 0 || nameRva >= imageSize) {
                            continue;
                        }
                        const char* exportedName = reinterpret_cast<const char*>(base + nameRva);
                        if (std::strcmp(exportedName, symbolName) == 0) {
                            return true;
                        }
                    }
                    return false;
                }

                void addWin32DllSearchDir(
                    const std::filesystem::path& dir,
                    std::unordered_set<std::string>& registeredSet,
                    std::vector<void*>& outCookies)
                {
                    if (dir.empty()) {
                        return;
                    }
                    std::error_code ec;
                    if (!std::filesystem::is_directory(dir, ec)) {
                        return;
                    }
                    const std::string key = std::filesystem::absolute(dir, ec).lexically_normal().generic_string();
                    if (!registeredSet.insert(key).second) {
                        return;
                    }
                    const std::wstring wideDir = std::filesystem::absolute(dir, ec).lexically_normal().wstring();
                    if (DLL_DIRECTORY_COOKIE cookie = AddDllDirectory(wideDir.c_str()); cookie != nullptr) {
                        outCookies.push_back(cookie);
                    }
                }
#endif

                void* openNativeScriptLibrary(const std::string& dllPath, std::string& outError)
                {
                    outError.clear();
#ifdef _WIN32
                    const std::wstring wideDllPath = std::filesystem::path(dllPath).wstring();
                    constexpr DWORD kFlags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                             LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                             LOAD_LIBRARY_SEARCH_USER_DIRS |
                                             LOAD_LIBRARY_SEARCH_SYSTEM32;
                    HMODULE hMod = LoadLibraryExW(wideDllPath.c_str(), nullptr, kFlags);
                    if (!hMod) {
                        hMod = LoadLibraryW(wideDllPath.c_str());
                    }
                    if (!hMod) {
                        outError = "Win32 Error " + std::to_string(GetLastError());
                        return nullptr;
                    }
                    return static_cast<void*>(hMod);
#elif !defined(__EMSCRIPTEN__)
                    void* handle = dlopen(dllPath.c_str(), RTLD_NOW | RTLD_LOCAL);
                    if (!handle) {
                        const char* err = dlerror();
                        outError = err ? err : "dlopen failed";
                    }
                    return handle;
#else
                    (void)dllPath;
                    outError = "当前平台不支持动态库加载";
                    return nullptr;
#endif
                }

                void* resolveScriptSymbol(void* handle, const char* symbolName) noexcept
                {
                    if (!handle || !symbolName) {
                        return nullptr;
                    }
#ifdef _WIN32
                    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), symbolName));
#elif !defined(__EMSCRIPTEN__)
                    return dlsym(handle, symbolName);
#else
                    return nullptr;
#endif
                }

                void closeNativeScriptLibrary(void* handle) noexcept
                {
                    if (!handle) {
                        return;
                    }
#ifdef _WIN32
                    FreeLibrary(static_cast<HMODULE>(handle));
#elif !defined(__EMSCRIPTEN__)
                    dlclose(handle);
#endif
                }

                bool hasActiveSlotForClass(
                    const Scene::NativeScriptComponent& scriptComp,
                    std::string_view className) noexcept
                {
                    if (!scriptComp.scripts.empty()) {
                        for (const auto& slot : scriptComp.scripts) {
                            if (slot.enabled && slot.className == className) {
                                return true;
                            }
                        }
                        return false;
                    }
                    return !scriptComp.scriptClassName.empty() && scriptComp.scriptClassName == className;
                }
            } // namespace

            // ==================== NativeScript 上下文实现 ====================

            Scene::SceneSubsystem* NativeScript::tryGetScene() const noexcept
            {
                auto* dom = Domain::InstancePtr();
                return dom ? dom->getScene() : nullptr;
            }

            HID::HIDSubsystem* NativeScript::tryGetHid() const noexcept
            {
                auto* dom = Domain::InstancePtr();
                return dom ? dom->getHID() : nullptr;
            }

            Renderer::RendererSubsystem* NativeScript::tryGetRenderer() const noexcept
            {
                auto* dom = Domain::InstancePtr();
                return dom ? dom->getRenderer() : nullptr;
            }

            NativeScript* NativeScript::findLiveScriptByTypeHashImpl(size_t typeHash) const noexcept
            {
                if (!m_gameplay) {
                    return nullptr;
                }
                return m_gameplay->findLiveScriptByTypeHash(m_registry, typeHash);
            }

            NativeScript* NativeScript::findLiveScriptOnEntityByTypeHashImpl(
                FCT::ECS::entity targetEntity,
                size_t typeHash) const noexcept
            {
                if (!m_gameplay || !m_registry || targetEntity == FCT::ECS::null) {
                    return nullptr;
                }
                return m_gameplay->findLiveScriptOnEntityByTypeHash(m_registry, targetEntity, typeHash);
            }

            void RegisterNativeScriptDescriptorInSubsystem(
                GamePlaySubsystem& gameplay,
                NativeScriptClassDescriptor descriptor)
            {
                gameplay.registerNativeScriptDescriptor(std::move(descriptor));
            }

            // ==================== GameStateMachine ====================

            void GameStateMachine::transitionTo(GameState newState)
            {
                if (m_currentState == newState) {
                    return;
                }
                const GameState oldState = m_currentState;
                m_currentState = newState;

                for (const auto& cb : m_callbacks) {
                    if (cb) {
                        cb(oldState, newState);
                    }
                }
            }

            void GameStateMachine::onStateChanged(StateChangeCallback callback)
            {
                m_callbacks.push_back(std::move(callback));
            }

            // ==================== GamePlaySubsystem ====================

            GamePlaySubsystem::GamePlaySubsystem() = default;

            GamePlaySubsystem::~GamePlaySubsystem()
            {
                term();
            }

            void GamePlaySubsystem::registerInfo()
            {
            }

            Status GamePlaySubsystem::init()
            {
                if (m_initialized.load(std::memory_order_relaxed)) {
                    return OkStatus();
                }

                m_stateMachine.transitionTo(GameState::Playing);
                m_initialized.store(true, std::memory_order_release);
                return OkStatus();
            }

            void GamePlaySubsystem::term() noexcept
            {
                shutdownAllNativeScriptInstances(nullptr);
                unloadNativeScriptLibraries();
                m_scriptDescriptors.clear();
                m_timers.clear();
                m_tickCallbacks.clear();
                m_initialized.store(false, std::memory_order_release);
            }

            void GamePlaySubsystem::update(float deltaTime, FCT::ECS::registry& registry)
            {
                if (!m_initialized.load(std::memory_order_acquire)) {
                    return;
                }

                ZoneScopedN("GamePlaySubsystem::update");

                updateTimers(deltaTime);

                for (size_t i = 0; i < m_tickCallbacks.size(); ++i) {
                    if (m_tickCallbacks[i].callback) {
                        m_tickCallbacks[i].callback(deltaTime);
                    }
                }

                auto behaviorView = registry.view<BehaviorComponent>();
                for (auto entity : behaviorView) {
                    auto& behavior = behaviorView.get<BehaviorComponent>(entity);
                    if (!behavior.started) {
                        behavior.started = true;
                        if (behavior.onStart) {
                            behavior.onStart(entity);
                        }
                    }
                    if (m_stateMachine.getCurrentState() == GameState::Playing && behavior.onUpdate) {
                        behavior.onUpdate(entity, deltaTime);
                    }
                }

                updateNativeScripts(deltaTime, registry);
            }

            void GamePlaySubsystem::fixedUpdate(float fixedDeltaTime, FCT::ECS::registry& registry)
            {
                if (!m_initialized.load(std::memory_order_acquire) ||
                    m_stateMachine.getCurrentState() != GameState::Playing) {
                    return;
                }

                ZoneScopedN("GamePlaySubsystem::fixedUpdate");

                auto behaviorView = registry.view<BehaviorComponent>();
                for (auto entity : behaviorView) {
                    auto& behavior = behaviorView.get<BehaviorComponent>(entity);
                    if (behavior.started && behavior.onFixedUpdate) {
                        behavior.onFixedUpdate(entity, fixedDeltaTime);
                    }
                }

                fixedUpdateNativeScripts(fixedDeltaTime, registry);
            }

            void GamePlaySubsystem::reconcileStaleNativeScriptInstances(FCT::ECS::registry& registry)
            {
                for (size_t i = m_liveScripts.size(); i > 0; --i) {
                    const size_t idx = i - 1;
                    LiveNativeScriptRecord& rec = m_liveScripts[idx];
                    if (rec.registry != &registry) {
                        continue;
                    }

                    bool keepAlive = false;
                    if (rec.entity != FCT::ECS::null &&
                        registry.valid(rec.entity) &&
                        !registry.all_of<Scene::DisabledTag>(rec.entity)) {
                        if (const auto* scriptComp = registry.try_get<Scene::NativeScriptComponent>(rec.entity)) {
                            keepAlive = hasActiveSlotForClass(*scriptComp, rec.className);
                        }
                    }

                    if (!keepAlive) {
                        if (rec.instance) {
                            rec.instance->onDestroy();
                            rec.instance.reset();
                        }
                        m_liveScripts.erase(m_liveScripts.begin() + static_cast<ptrdiff_t>(idx));
                    }
                }
            }

            void GamePlaySubsystem::updateNativeScripts(float deltaTime, FCT::ECS::registry& registry)
            {
                ZoneScopedN("GamePlaySubsystem::updateNativeScripts");

                // 1. 先回收被禁用、被移除组件或已销毁实体的脚本实例
                reconcileStaleNativeScriptInstances(registry);

                // 2. 收集激活实体并按实体句柄升序确定性排序（保证父/主控实体先于后续实体初始化）
                auto scriptView = registry.view<Scene::NativeScriptComponent>(
                    FCT::ECS::exclude<Scene::DisabledTag>);
                std::vector<FCT::ECS::entity> sortedEntities;
                sortedEntities.reserve(scriptView.size_hint());
                for (auto entity : scriptView) {
                    sortedEntities.push_back(entity);
                }
                std::sort(sortedEntities.begin(), sortedEntities.end(), [](FCT::ECS::entity a, FCT::ECS::entity b) {
                    return static_cast<uint32_t>(FCT::ECS::to_integral(a)) <
                           static_cast<uint32_t>(FCT::ECS::to_integral(b));
                });

                Runtime::Engine* enginePtr = Runtime::Engine::getInstance();

                // 3. 阶段 A (onCreate)：先完成本帧所有新建脚本实例的构造、属性注入与 onCreate，确保跨实体脚本互查就绪
                for (auto entity : sortedEntities) {
                    if (!registry.valid(entity)) {
                        continue;
                    }
                    auto* scriptComp = registry.try_get<Scene::NativeScriptComponent>(entity);
                    if (!scriptComp) {
                        continue;
                    }
                    Scene::NormalizeNativeScriptSlots(*scriptComp);

                    for (auto& slot : scriptComp->scripts) {
                        if (!slot.enabled || slot.className.empty()) {
                            continue;
                        }
                        if (getLiveNativeScript(registry, entity, slot.className) != nullptr) {
                            continue;
                        }
                        const auto* desc = getNativeScriptDescriptor(slot.className);
                        if (!desc || !desc->factory) {
                            continue;
                        }

                        UniquePtr<NativeScript> newInst = desc->factory();
                        if (!newInst) {
                            continue;
                        }
                        newInst->bindRuntimeContext(entity, &registry, enginePtr, this);
                        if (desc->applyProperties && !slot.properties.empty()) {
                            desc->applyProperties(newInst.get(), slot.properties);
                        }
                        newInst->onCreate();
                        if (desc->extractProperties && slot.properties.empty()) {
                            desc->extractProperties(newInst.get(), slot.properties);
                        }

                        LiveNativeScriptRecord rec{};
                        rec.registry = &registry;
                        rec.entity = entity;
                        rec.className = slot.className;
                        rec.moduleOwner = desc->moduleOwner;
                        rec.typeHash = desc->typeHash;
                        rec.started = false;
                        rec.instance = std::move(newInst);
                        m_liveScripts.push_back(std::move(rec));
                    }
                }

                // 4. 阶段 B (onStart)：所有实例就位后统一触发首次 onStart（对标 Awake -> Start 双阶段契约）
                for (size_t i = 0; i < m_liveScripts.size(); ++i) {
                    if (m_liveScripts[i].registry != &registry || m_liveScripts[i].started) {
                        continue;
                    }
                    if (!registry.valid(m_liveScripts[i].entity) ||
                        registry.all_of<Scene::DisabledTag>(m_liveScripts[i].entity)) {
                        continue;
                    }
                    m_liveScripts[i].started = true;
                    if (m_liveScripts[i].instance) {
                        m_liveScripts[i].instance->onStart();
                    }
                }

                // 5. 阶段 C (onUpdate)：游戏处于 Playing 态时逐帧驱动逻辑更新
                if (m_stateMachine.getCurrentState() != GameState::Playing) {
                    return;
                }
                for (size_t i = 0; i < m_liveScripts.size(); ++i) {
                    if (m_liveScripts[i].registry != &registry || !m_liveScripts[i].started) {
                        continue;
                    }
                    const FCT::ECS::entity ent = m_liveScripts[i].entity;
                    if (ent == FCT::ECS::null || !registry.valid(ent) || registry.all_of<Scene::DisabledTag>(ent)) {
                        continue;
                    }
                    if (m_liveScripts[i].instance) {
                        m_liveScripts[i].instance->onUpdate(deltaTime);
                    }
                }
            }

            void GamePlaySubsystem::fixedUpdateNativeScripts(float fixedDeltaTime, FCT::ECS::registry& registry)
            {
                for (size_t i = 0; i < m_liveScripts.size(); ++i) {
                    if (m_liveScripts[i].registry != &registry || !m_liveScripts[i].started) {
                        continue;
                    }
                    const FCT::ECS::entity ent = m_liveScripts[i].entity;
                    if (ent == FCT::ECS::null || !registry.valid(ent) || registry.all_of<Scene::DisabledTag>(ent)) {
                        continue;
                    }
                    if (m_liveScripts[i].instance) {
                        m_liveScripts[i].instance->onFixedUpdate(fixedDeltaTime);
                    }
                }
            }

            void GamePlaySubsystem::notifyEntityDestroyed(FCT::ECS::entity entity, FCT::ECS::registry& registry)
            {
                if (auto* behavior = registry.try_get<BehaviorComponent>(entity)) {
                    if (behavior->onDestroy) {
                        behavior->onDestroy(entity);
                    }
                }

                for (size_t i = m_liveScripts.size(); i > 0; --i) {
                    const size_t idx = i - 1;
                    if (m_liveScripts[idx].registry == &registry && m_liveScripts[idx].entity == entity) {
                        if (m_liveScripts[idx].instance) {
                            m_liveScripts[idx].instance->onDestroy();
                            m_liveScripts[idx].instance.reset();
                        }
                        m_liveScripts.erase(m_liveScripts.begin() + static_cast<ptrdiff_t>(idx));
                    }
                }
            }

            void GamePlaySubsystem::registerNativeScriptDescriptor(NativeScriptClassDescriptor descriptor)
            {
                assert(!descriptor.className.empty() && descriptor.factory != nullptr &&
                       "注册原生脚本描述符失败：类名或工厂函数不能为空");
                const std::string key = descriptor.className;
                m_scriptDescriptors.insert_or_assign(key, std::move(descriptor));
            }

            void GamePlaySubsystem::unregisterNativeScriptsByOwner(std::string_view moduleOwner)
            {
                if (moduleOwner.empty()) {
                    return;
                }

                for (size_t i = m_liveScripts.size(); i > 0; --i) {
                    const size_t idx = i - 1;
                    if (m_liveScripts[idx].moduleOwner == moduleOwner) {
                        if (m_liveScripts[idx].instance) {
                            m_liveScripts[idx].instance->onDestroy();
                            m_liveScripts[idx].instance.reset();
                        }
                        m_liveScripts.erase(m_liveScripts.begin() + static_cast<ptrdiff_t>(idx));
                    }
                }

                for (auto it = m_scriptDescriptors.begin(); it != m_scriptDescriptors.end();) {
                    if (it->second.moduleOwner == moduleOwner) {
                        it = m_scriptDescriptors.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            std::vector<std::string> GamePlaySubsystem::getRegisteredNativeScriptNames() const
            {
                std::vector<std::string> names;
                names.reserve(m_scriptDescriptors.size());
                for (const auto& kv : m_scriptDescriptors) {
                    names.push_back(kv.first);
                }
                std::sort(names.begin(), names.end());
                return names;
            }

            const NativeScriptClassDescriptor* GamePlaySubsystem::getNativeScriptDescriptor(
                std::string_view className) const noexcept
            {
                if (className.empty()) {
                    return nullptr;
                }
                auto it = m_scriptDescriptors.find(std::string(className));
                return (it != m_scriptDescriptors.end()) ? &it->second : nullptr;
            }

            NativeScript* GamePlaySubsystem::getLiveNativeScript(
                const FCT::ECS::registry& registry,
                FCT::ECS::entity entity,
                std::string_view className) const noexcept
            {
                for (const auto& rec : m_liveScripts) {
                    if (rec.registry == &registry && rec.entity == entity && rec.className == className) {
                        return rec.instance.get();
                    }
                }
                return nullptr;
            }

            NativeScript* GamePlaySubsystem::findLiveScriptByTypeHash(
                const FCT::ECS::registry* preferredRegistry,
                size_t typeHash) const noexcept
            {
                if (preferredRegistry) {
                    for (const auto& rec : m_liveScripts) {
                        if (rec.registry == preferredRegistry && rec.typeHash == typeHash && rec.instance) {
                            return rec.instance.get();
                        }
                    }
                }
                for (const auto& rec : m_liveScripts) {
                    if (rec.typeHash == typeHash && rec.instance) {
                        return rec.instance.get();
                    }
                }
                return nullptr;
            }

            NativeScript* GamePlaySubsystem::findLiveScriptOnEntityByTypeHash(
                const FCT::ECS::registry* registry,
                FCT::ECS::entity entity,
                size_t typeHash) const noexcept
            {
                for (const auto& rec : m_liveScripts) {
                    if (rec.registry == registry &&
                        rec.entity == entity &&
                        rec.typeHash == typeHash &&
                        rec.instance) {
                        return rec.instance.get();
                    }
                }
                return nullptr;
            }

            bool GamePlaySubsystem::syncLiveScriptPropertiesToSlot(
                const FCT::ECS::registry& registry,
                FCT::ECS::entity entity,
                Scene::NativeScriptSlot& slot) const
            {
                const NativeScript* liveInst = getLiveNativeScript(registry, entity, slot.className);
                const auto* desc = getNativeScriptDescriptor(slot.className);
                if (!liveInst || !desc || !desc->extractProperties) {
                    return false;
                }
                desc->extractProperties(liveInst, slot.properties);
                return true;
            }

            bool GamePlaySubsystem::applySlotPropertiesToLiveScript(
                const FCT::ECS::registry& registry,
                FCT::ECS::entity entity,
                const Scene::NativeScriptSlot& slot)
            {
                NativeScript* liveInst = getLiveNativeScript(registry, entity, slot.className);
                const auto* desc = getNativeScriptDescriptor(slot.className);
                if (!liveInst || !desc || !desc->applyProperties) {
                    return false;
                }
                desc->applyProperties(liveInst, slot.properties);
                return true;
            }

            void GamePlaySubsystem::shutdownAllNativeScriptInstances(FCT::ECS::registry* targetRegistry) noexcept
            {
                for (size_t i = m_liveScripts.size(); i > 0; --i) {
                    const size_t idx = i - 1;
                    if (targetRegistry != nullptr && m_liveScripts[idx].registry != targetRegistry) {
                        continue;
                    }
                    if (m_liveScripts[idx].instance) {
                        m_liveScripts[idx].instance->onDestroy();
                        m_liveScripts[idx].instance.reset();
                    }
                    m_liveScripts.erase(m_liveScripts.begin() + static_cast<ptrdiff_t>(idx));
                }
            }

            Status GamePlaySubsystem::loadProjectNativeScriptLibraries(const std::string& nativeDir)
            {
                ZoneScoped();
                if (nativeDir.empty()) {
                    return OkStatus();
                }

                const std::string normalizedDir = normalizeScriptLibraryPath(nativeDir);
                if (normalizedDir.empty()) {
                    return OkStatus();
                }

                if (!m_activeNativeScriptDir.empty() && m_activeNativeScriptDir != normalizedDir) {
                    unloadNativeScriptLibraries();
                }

                auto* loader = resolveGameplayResourceLoader();
                if (!loader || !loader->accessDir(nativeDir).ok()) {
                    return OkStatus();
                }

#ifdef _WIN32
                std::unordered_set<std::string> registeredSearchDirs;
                if (auto* res = Resource::Resource::InstancePtr()) {
                    if (auto* cfg = res->getConfig()) {
                        addWin32DllSearchDir(cfg->getEngineDir(), registeredSearchDirs, m_dllDirectoryCookies);
                        addWin32DllSearchDir(cfg->getEnginePluginsDir(), registeredSearchDirs, m_dllDirectoryCookies);
                        addWin32DllSearchDir(cfg->getProjectDir(), registeredSearchDirs, m_dllDirectoryCookies);
                        addWin32DllSearchDir(cfg->getProjectPluginsDir(), registeredSearchDirs, m_dllDirectoryCookies);
                        std::error_code ec;
                        const std::filesystem::path pluginsRoot(cfg->getEnginePluginsDir());
                        if (std::filesystem::is_directory(pluginsRoot, ec)) {
                            for (const auto& sub : std::filesystem::directory_iterator(pluginsRoot, ec)) {
                                if (sub.is_directory(ec)) {
                                    addWin32DllSearchDir(sub.path(), registeredSearchDirs, m_dllDirectoryCookies);
                                }
                            }
                        }
                    }
                }
                addWin32DllSearchDir(normalizedDir, registeredSearchDirs, m_dllDirectoryCookies);
#endif

                auto dirOr = loader->readDir(nativeDir);
                if (!dirOr.ok()) {
                    return dirOr.status();
                }

                m_activeNativeScriptDir = normalizedDir;
                using RegisterScriptsFn = void (*)(GamePlaySubsystem*, const char*);

                for (const auto& entry : dirOr.value()) {
                    if (!isSharedLibraryEntry(entry.get())) {
                        continue;
                    }
                    const std::string absDllPath = normalizeScriptLibraryPath(entry->path());
                    const bool alreadyLoaded = std::any_of(
                        m_loadedScriptLibraries.begin(),
                        m_loadedScriptLibraries.end(),
                        [&absDllPath](const LoadedNativeScriptLibrary& lib) {
                            return lib.normalizedPath == absDllPath;
                        });
                    if (alreadyLoaded) {
                        continue;
                    }

#ifdef _WIN32
                    if (!peHasExportSymbol(absDllPath, kRegisterNativeScriptsSymbol)) {
                        continue;
                    }
#endif
                    std::string loadError;
                    void* handle = openNativeScriptLibrary(absDllPath, loadError);
                    if (!handle) {
                        spdlog::warn("[GamePlaySubsystem] 加载工程原生脚本动态库失败: '{}' ({})", absDllPath, loadError);
                        continue;
                    }

                    auto registerFn = reinterpret_cast<RegisterScriptsFn>(
                        resolveScriptSymbol(handle, kRegisterNativeScriptsSymbol));
                    if (!registerFn) {
                        closeNativeScriptLibrary(handle);
                        continue;
                    }

                    const std::string modName = entry->stem();
                    registerFn(this, modName.c_str());

                    LoadedNativeScriptLibrary loaded{};
                    loaded.normalizedPath = absDllPath;
                    loaded.moduleName = modName;
                    loaded.handle = handle;
                    m_loadedScriptLibraries.push_back(std::move(loaded));
                    spdlog::info(
                        "[GamePlaySubsystem] 已加载工程原生脚本库 '{}' (当前已注册 C++ 脚本类: {} 个)",
                        modName,
                        m_scriptDescriptors.size());
                }

                return OkStatus();
            }

            void GamePlaySubsystem::unloadNativeScriptLibraries() noexcept
            {
                if (m_loadedScriptLibraries.empty()) {
                    m_activeNativeScriptDir.clear();
                    return;
                }

                shutdownAllNativeScriptInstances(nullptr);

                if (auto* host = Host::RenderHost::InstancePtr()) {
                    if (auto* ctx = host->getContext()) {
                        ctx->_consumeLogicTask();
                        ctx->_waitCurrentFlush();
                        ctx->waitGpu();
                    }
                }
                if (auto* gpuMem = Resource::GpuMemoryManager::InstancePtr()) {
                    gpuMem->flushAllDeferredReleases();
                }

                for (size_t i = m_loadedScriptLibraries.size(); i > 0; --i) {
                    auto& lib = m_loadedScriptLibraries[i - 1];
                    unregisterNativeScriptsByOwner(lib.moduleName);
                    closeNativeScriptLibrary(lib.handle);
                    lib.handle = nullptr;
                }
                m_loadedScriptLibraries.clear();
                m_activeNativeScriptDir.clear();

#ifdef _WIN32
                for (auto it = m_dllDirectoryCookies.rbegin(); it != m_dllDirectoryCookies.rend(); ++it) {
                    if (*it != nullptr) {
                        RemoveDllDirectory(static_cast<DLL_DIRECTORY_COOKIE>(*it));
                    }
                }
                m_dllDirectoryCookies.clear();
#endif
            }

            void GamePlaySubsystem::registerTickCallback(const std::string& name, GameplayTickCallback cb)
            {
                assert(!name.empty() && cb != nullptr && "注册 Gameplay Tick 回调失败：名称或回调为空");
                for (auto& item : m_tickCallbacks) {
                    if (item.name == name) {
                        item.callback = std::move(cb);
                        return;
                    }
                }
                m_tickCallbacks.push_back({name, std::move(cb)});
            }

            void GamePlaySubsystem::unregisterTickCallback(const std::string& name)
            {
                std::erase_if(m_tickCallbacks, [&name](const NamedTickCallback& item) {
                    return item.name == name;
                });
            }

            uint64_t GamePlaySubsystem::setTimer(float durationSec, bool repeating, std::function<void()> cb)
            {
                assert(cb != nullptr && durationSec >= 0.0f && "定时器参数非法");
                const uint64_t id = m_nextTimerId++;
                m_timers.push_back({id, durationSec, 0.0f, repeating, std::move(cb)});
                return id;
            }

            void GamePlaySubsystem::clearTimer(uint64_t timerId)
            {
                std::erase_if(m_timers, [timerId](const GameplayTimer& t) {
                    return t.id == timerId;
                });
            }

            void GamePlaySubsystem::pauseGame()
            {
                m_stateMachine.transitionTo(GameState::Paused);
            }

            void GamePlaySubsystem::resumeGame()
            {
                m_stateMachine.transitionTo(GameState::Playing);
            }

            void GamePlaySubsystem::restartGame()
            {
                m_stateMachine.transitionTo(GameState::Playing);
            }

            void GamePlaySubsystem::updateTimers(float deltaTime)
            {
                for (size_t i = 0; i < m_timers.size();) {
                    auto& t = m_timers[i];
                    t.elapsed += deltaTime;
                    if (t.elapsed >= t.duration) {
                        if (t.callback) {
                            t.callback();
                        }
                        if (t.repeating) {
                            t.elapsed = 0.0f;
                            ++i;
                        } else {
                            m_timers.erase(m_timers.begin() + static_cast<ptrdiff_t>(i));
                        }
                    } else {
                        ++i;
                    }
                }
            }

        } // namespace GamePlay
    } // namespace Domain
} // namespace MQEngine
