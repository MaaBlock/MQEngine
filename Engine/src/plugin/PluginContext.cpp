#include "PluginContext.h"
#include "../core/engine.h"
#include "../manager/TechManager.h"
#include "../manager/SystemManager.h"
#include "../system/ISystem.h"
#include "../core/Tech.h"

namespace MQEngine {

using absl::FailedPreconditionError;

Status PluginContext::registerTech(const std::string& passName, Tech&& tech) {
    if (!techManager) {
        return FailedPreconditionError("PluginContext: TechManager 指针为空，无法注册 Technique");
    }
    return techManager->addTech(passName, std::move(tech));
}

Status PluginContext::registerPass(const FCT::PassDesc& desc) {
    if (!renderGraph) {
        return FailedPreconditionError("PluginContext: RenderGraph 指针为空，无法注册 Pass");
    }
    renderGraph->addPassDesc(desc);
    return OkStatus();
}

void PluginContext::registerSystem(const std::string& name, ISystem* system,
                                   const std::vector<std::string>& pre,
                                   const std::vector<std::string>& succ) {
    if (systemManager && system) {
        systemManager->requestAddSystem(name, system, pre, succ);
    }
}

} // namespace MQEngine
