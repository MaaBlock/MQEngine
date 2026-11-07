#ifndef MQENGINE_ISCRIPTBACKEND_H
#define MQENGINE_ISCRIPTBACKEND_H

#include "../../EnginePCH.h"
#include "../../thirdparty/thirdparty.h"
#include <string>
#include <vector>

namespace MQEngine {

    class ResourceLoader;

    /**
     * @brief 脚本后端运行时类型
     */
    enum class ScriptBackendType {
        JsTs,
        Pybind
    };

    /**
     * @brief 跨语言脚本后端统一接口（支持 Node/NBR JS/TS 与 pybind11 Python）
     */
    class ENGINE_API IScriptBackend {
    public:
        virtual ~IScriptBackend() = default;

        virtual ScriptBackendType getType() const = 0;
        virtual const char* getName() const = 0;
        virtual bool isAvailable() const = 0;

        virtual Status initialize() = 0;
        virtual void shutdown() = 0;

        virtual Status loadScriptsFromRoot(const std::string& scriptsRoot) = 0;
        virtual Status reloadScriptsFromRoot(const std::string& scriptsRoot) = 0;
        virtual void exportTypeDefinitions(const std::string& scriptsRoot) = 0;

        virtual void setLogicDeltaTime(float deltaTime) = 0;
        virtual void tick() = 0;

        virtual std::vector<std::string> getFunctionNames() const = 0;
        virtual std::vector<std::string> getClassNames() const = 0;

        virtual Status executeModuleScript(const std::string& moduleName,
                                           const std::string& moduleVersion,
                                           const std::string& entryPath,
                                           const std::string& moduleDir,
                                           ResourceLoader* resourceLoader) = 0;

        virtual Status invokeModuleLifecycle(const std::string& moduleName,
                                             const std::string& methodName,
                                             const std::string& argsJson = "{}") = 0;

        virtual void unloadModuleScript(const std::string& moduleName) = 0;
    };

} // namespace MQEngine

#endif // MQENGINE_ISCRIPTBACKEND_H
