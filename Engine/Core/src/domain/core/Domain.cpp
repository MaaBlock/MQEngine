/**
 * @file Domain.cpp
 * @brief Domain 层顶层门面实现
 */
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/host/logic/LogicHost.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/runtime/core/Engine.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <string_view>

namespace MQEngine
{
    namespace Domain
    {
        Domain::Domain() = default;

        Domain::~Domain()
        {
            term();
        }

        void Domain::registerInfo()
        {
            js.registerInfo();
            py.registerInfo();
            physics.registerInfo();
            hid.registerInfo();
            debug.registerInfo();
            renderer.registerInfo();
            scene.registerInfo();
            animation.registerInfo();
            effect.registerInfo();
            gameplay.registerInfo();
        }

        Status Domain::init()
        {
            if (m_initialized) {
                return OkStatus();
            }

            CHECK_STATUS(js.init());
            CHECK_STATUS(py.init());
            CHECK_STATUS(physics.init());
            CHECK_STATUS(hid.init());
            CHECK_STATUS(debug.init());
            CHECK_STATUS(scene.init());
            CHECK_STATUS(renderer.init());
            CHECK_STATUS(animation.init());
            CHECK_STATUS(effect.init());
            CHECK_STATUS(gameplay.init());

            if (auto* res = Resource::Resource::InstancePtr())
            {
                const std::string nativeDir = res->getProjectNativeDir();
                if (!nativeDir.empty())
                {
                    CHECK_STATUS(gameplay.loadProjectNativeScriptLibraries(nativeDir));
                }
            }

            m_initialized = true;

            std::vector<std::string> initialTags{"Runtime"};
            if (auto* engine = Runtime::Engine::getInstance(); engine && !engine->getActiveApplicationTags().empty()) {
                initialTags = engine->getActiveApplicationTags();
            }
            CHECK_STATUS(reconcileApplicationTags(initialTags));
            return OkStatus();
        }

        void Domain::term() noexcept
        {
            if (!m_initialized) {
                return;
            }

            unregisterTickersFromLogicHost();

            gameplay.term();
            effect.term();
            animation.term();
            renderer.term();
            scene.term();
            debug.term();
            hid.term();
            physics.term();
            py.term();
            js.term();

            m_initialized = false;
        }

        Status Domain::reconcileApplicationTags(const std::vector<std::string>& activeTags)
        {
            if (!m_initialized) {
                return OkStatus();
            }

            auto containsTag = [&activeTags](std::string_view tag) {
                return std::find(activeTags.begin(), activeTags.end(), tag) != activeTags.end();
            };

            const bool attachEditorMetadata = containsTag("Editor") || containsTag("CliEditor");
            scene.setEditorMetadataEnabled(attachEditorMetadata);

            if (containsTag("Runtime")) {
                activateSceneTicker();
            } else {
                deactivateSceneTicker();
            }

            if (containsTag("Game")) {
                activateGameplayTicker();
                physics.activateRuntimeTicker();
            } else {
                physics.deactivateRuntimeTicker();
                deactivateGameplayTicker();
            }

            return OkStatus();
        }

        void Domain::activateSceneTicker()
        {
            auto* logicHost = Host::LogicHost::InstancePtr();
            if (!logicHost || logicHost->tickers().hasNode("Domain_Scene_Update")) {
                return;
            }

            logicHost->tickers().addNode(
                "Domain_Scene_Update",
                [this, lastTick = std::chrono::steady_clock::now(), firstTick = true]() mutable {
                    ZoneScopedN("Domain::SceneUpdate");
                    const auto now = std::chrono::steady_clock::now();
                    float realDt = firstTick
                        ? 0.016667f
                        : std::chrono::duration<float>(now - lastTick).count();
                    firstTick = false;
                    lastTick = now;
                    realDt = std::clamp(realDt, 0.0001f, 0.25f);

                    scene.update();
                    {
                        ZoneScopedN("DebugSubsystem::tickGizmos");
                        debug.tickGizmos(realDt);
                    }
                });
        }

        void Domain::deactivateSceneTicker() noexcept
        {
            auto* logicHost = Host::LogicHost::InstancePtr();
            if (logicHost) {
                logicHost->tickers().removeNode("Domain_Scene_Update");
            }
        }

        void Domain::activateGameplayTicker()
        {
            auto* logicHost = Host::LogicHost::InstancePtr();
            if (!logicHost || logicHost->tickers().hasNode("Domain_Gameplay_Update")) {
                return;
            }

            logicHost->tickers().addNode(
                "Domain_Gameplay_Update",
                [this, lastTick = std::chrono::steady_clock::now(), firstTick = true]() mutable {
                    ZoneScopedN("Domain::GameplayUpdate");
                    const auto now = std::chrono::steady_clock::now();
                    float realDt = firstTick
                        ? 0.016667f
                        : std::chrono::duration<float>(now - lastTick).count();
                    firstTick = false;
                    lastTick = now;
                    realDt = std::clamp(realDt, 0.0001f, 0.25f);

                    {
                        ZoneScopedN("AnimationSubsystem::update");
                        animation.update(realDt, scene.getRegistry());
                    }
                    {
                        ZoneScopedN("EffectSubsystem::update");
                        effect.update(realDt, scene.getRegistry());
                    }
                    {
                        ZoneScopedN("GameplaySubsystem::update");
                        gameplay.update(realDt, scene.getRegistry());
                    }
                },
                {"Domain_Scene_Update"},
                {"Asset_Composite_Tick"});
        }

        void Domain::deactivateGameplayTicker() noexcept
        {
            auto* logicHost = Host::LogicHost::InstancePtr();
            if (logicHost)
            {
                logicHost->tickers().removeNode("Domain_Gameplay_Update");
            }
            gameplay.shutdownAllNativeScriptInstances(nullptr);
        }

        void Domain::unregisterTickersFromLogicHost() noexcept
        {
            deactivateGameplayTicker();
            deactivateSceneTicker();
        }

    } // namespace Domain
} // namespace MQEngine
