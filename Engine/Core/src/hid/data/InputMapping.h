#ifndef MQENGINE_INPUTMAPPING_H
#define MQENGINE_INPUTMAPPING_H

#include <string>
#include <vector>
#include <unordered_map>
#include "KeyCode.h"
#include "../../thirdparty/thirdparty.h"

namespace MQEngine {

    class ResourceLoader;

    struct ActionMapping {
        std::string name;
        std::vector<KeyCode> keys;
    };

    struct AxisKeyBinding {
        KeyCode key = KeyCode::Unknown;
        float scale = 1.0f;
    };

    struct AxisMouseBinding {
        MouseAxis axis = MouseAxis::None;
        float scale = 1.0f;
    };

    struct AxisMapping {
        std::string name;
        std::vector<AxisKeyBinding> keyBindings;
        std::vector<AxisMouseBinding> mouseBindings;
        float deadzone = 0.0f;
        bool clamp = false;
        float clampMin = -1.0f;
        float clampMax = 1.0f;
        float sensitivity = 1.0f;
    };

    class ENGINE_API InputMapping {
    public:
        InputMapping();
        ~InputMapping() = default;

        // 动作映射操作 (Action Mappings)
        void addActionMapping(const std::string& actionName, KeyCode key);
        void addActionMapping(const std::string& actionName, MouseButton button);
        void removeActionMapping(const std::string& actionName, KeyCode key);
        void clearActionMappings(const std::string& actionName);
        bool hasActionMapping(const std::string& actionName) const;
        const ActionMapping* findActionMapping(const std::string& actionName) const;
        std::vector<std::string> getAllActionNames() const;

        // 轴向映射操作 (Axis Mappings)
        void addAxisMapping(const std::string& axisName, KeyCode key, float scale = 1.0f);
        void addAxisMapping(const std::string& axisName, MouseAxis mouseAxis, float scale = 1.0f);
        void removeAxisMapping(const std::string& axisName, KeyCode key);
        void removeAxisMapping(const std::string& axisName, MouseAxis mouseAxis);
        void clearAxisMappings(const std::string& axisName);
        bool hasAxisMapping(const std::string& axisName) const;
        const AxisMapping* findAxisMapping(const std::string& axisName) const;
        std::vector<std::string> getAllAxisNames() const;

        void setAxisDeadzone(const std::string& axisName, float deadzone);
        void setAxisClamp(const std::string& axisName, bool clamp, float minVal = -1.0f, float maxVal = 1.0f);
        void setAxisSensitivity(const std::string& axisName, float sensitivity);

        // 预设生成
        static UniquePtr<InputMapping> createDefaultFpsPreset();

        // 序列化与文件持久化
        std::string serializeToJson() const;
        Status deserializeFromJson(const std::string& jsonString);
        Status saveToFile(ResourceLoader* loader, const std::string& filePath) const;
        Status loadFromFile(ResourceLoader* loader, const std::string& filePath);

    private:
        std::unordered_map<std::string, ActionMapping> m_actionMappings;
        std::unordered_map<std::string, AxisMapping> m_axisMappings;
    };

} // namespace MQEngine

#endif // MQENGINE_INPUTMAPPING_H
