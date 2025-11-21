#include "SkyboxCacheSystem.h"
#include "../data/Component.h"
#include "../data/DataManager.h"
#include <vector>
#include <string>

namespace MQEngine {
    SkyboxCacheSystem::SkyboxCacheSystem(FCT::Context* ctx, DataManager* dataManager)
        : m_ctx(ctx), m_dataManager(dataManager) {}

    void SkyboxCacheSystem::updateLogic() {
        auto registries = m_dataManager->currentRegistries();
        for (auto registry : registries) {
            auto view = registry->view<SkyboxComponent>();
            for (auto entity : view) {
                if (!registry->all_of<CacheSkyboxComponent>(entity)) {
                    auto& skyboxComp = registry->get<SkyboxComponent>(entity);
                    std::string path = skyboxComp.texturePath;

                    if (path.empty()) continue;

                    if (path.back() != '/' && path.back() != '\\') {
                        path += "/";
                    }

                    std::vector<std::string> faces = {
                        path + "right.jpg",
                        path + "left.jpg",
                        path + "top.jpg",
                        path + "bottom.jpg",
                        path + "front.jpg",
                        path + "back.jpg"
                    };

                    FCT::Image* texture = m_ctx->loadCubeMap(faces);
                    if (texture) {
                        auto& cache = registry->emplace<CacheSkyboxComponent>(entity);
                        cache.texture = texture;
                        cache.visible = true;
                    } else {

                    }
                }
            }
        }
    }
}
