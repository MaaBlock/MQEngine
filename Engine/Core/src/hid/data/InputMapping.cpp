#include "InputMapping.h"
#include "../../resource/core/ResourceLoader.h"
#include <algorithm>

namespace MQEngine {

    InputMapping::InputMapping() = default;

    void InputMapping::addActionMapping(const std::string& actionName, KeyCode key) {
        if (actionName.empty() || key == KeyCode::Unknown) {
            return;
        }
        auto& mapping = m_actionMappings[actionName];
        mapping.name = actionName;
        if (std::find(mapping.keys.begin(), mapping.keys.end(), key) == mapping.keys.end()) {
            mapping.keys.push_back(key);
        }
    }

    void InputMapping::addActionMapping(const std::string& actionName, MouseButton button) {
        KeyCode key = mouseButtonToKeyCode(button);
        addActionMapping(actionName, key);
    }

    void InputMapping::removeActionMapping(const std::string& actionName, KeyCode key) {
        auto it = m_actionMappings.find(actionName);
        if (it != m_actionMappings.end()) {
            auto& keys = it->second.keys;
            keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
            if (keys.empty()) {
                m_actionMappings.erase(it);
            }
        }
    }

    void InputMapping::clearActionMappings(const std::string& actionName) {
        m_actionMappings.erase(actionName);
    }

    bool InputMapping::hasActionMapping(const std::string& actionName) const {
        return m_actionMappings.find(actionName) != m_actionMappings.end();
    }

    const ActionMapping* InputMapping::findActionMapping(const std::string& actionName) const {
        auto it = m_actionMappings.find(actionName);
        if (it != m_actionMappings.end()) {
            return &it->second;
        }
        return nullptr;
    }

    std::vector<std::string> InputMapping::getAllActionNames() const {
        std::vector<std::string> names;
        names.reserve(m_actionMappings.size());
        for (const auto& pair : m_actionMappings) {
            names.push_back(pair.first);
        }
        return names;
    }

    void InputMapping::addAxisMapping(const std::string& axisName, KeyCode key, float scale) {
        if (axisName.empty() || key == KeyCode::Unknown) {
            return;
        }
        auto& mapping = m_axisMappings[axisName];
        mapping.name = axisName;
        for (auto& binding : mapping.keyBindings) {
            if (binding.key == key) {
                binding.scale = scale;
                return;
            }
        }
        mapping.keyBindings.push_back({key, scale});
    }

    void InputMapping::addAxisMapping(const std::string& axisName, MouseAxis mouseAxis, float scale) {
        if (axisName.empty() || mouseAxis == MouseAxis::None) {
            return;
        }
        auto& mapping = m_axisMappings[axisName];
        mapping.name = axisName;
        for (auto& binding : mapping.mouseBindings) {
            if (binding.axis == mouseAxis) {
                binding.scale = scale;
                return;
            }
        }
        mapping.mouseBindings.push_back({mouseAxis, scale});
    }

    void InputMapping::removeAxisMapping(const std::string& axisName, KeyCode key) {
        auto it = m_axisMappings.find(axisName);
        if (it != m_axisMappings.end()) {
            auto& bindings = it->second.keyBindings;
            bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [key](const AxisKeyBinding& b) {
                return b.key == key;
            }), bindings.end());
        }
    }

    void InputMapping::removeAxisMapping(const std::string& axisName, MouseAxis mouseAxis) {
        auto it = m_axisMappings.find(axisName);
        if (it != m_axisMappings.end()) {
            auto& bindings = it->second.mouseBindings;
            bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [mouseAxis](const AxisMouseBinding& b) {
                return b.axis == mouseAxis;
            }), bindings.end());
        }
    }

    void InputMapping::clearAxisMappings(const std::string& axisName) {
        m_axisMappings.erase(axisName);
    }

    bool InputMapping::hasAxisMapping(const std::string& axisName) const {
        return m_axisMappings.find(axisName) != m_axisMappings.end();
    }

    const AxisMapping* InputMapping::findAxisMapping(const std::string& axisName) const {
        auto it = m_axisMappings.find(axisName);
        if (it != m_axisMappings.end()) {
            return &it->second;
        }
        return nullptr;
    }

    std::vector<std::string> InputMapping::getAllAxisNames() const {
        std::vector<std::string> names;
        names.reserve(m_axisMappings.size());
        for (const auto& pair : m_axisMappings) {
            names.push_back(pair.first);
        }
        return names;
    }

    void InputMapping::setAxisDeadzone(const std::string& axisName, float deadzone) {
        auto& mapping = m_axisMappings[axisName];
        mapping.name = axisName;
        mapping.deadzone = deadzone;
    }

    void InputMapping::setAxisClamp(const std::string& axisName, bool clamp, float minVal, float maxVal) {
        auto& mapping = m_axisMappings[axisName];
        mapping.name = axisName;
        mapping.clamp = clamp;
        mapping.clampMin = minVal;
        mapping.clampMax = maxVal;
    }

    void InputMapping::setAxisSensitivity(const std::string& axisName, float sensitivity) {
        auto& mapping = m_axisMappings[axisName];
        mapping.name = axisName;
        mapping.sensitivity = sensitivity;
    }

    UniquePtr<InputMapping> InputMapping::createDefaultFpsPreset() {
        auto mapping = makeUnique<InputMapping>();

        // 默认 FPS 动作映射
        mapping->addActionMapping("Jump", KeyCode::Space);
        mapping->addActionMapping("Fire", KeyCode::MouseLeft);
        mapping->addActionMapping("AltFire", KeyCode::MouseRight);
        mapping->addActionMapping("Reload", KeyCode::R);
        mapping->addActionMapping("Interact", KeyCode::E);
        mapping->addActionMapping("Crouch", KeyCode::LeftControl);
        mapping->addActionMapping("Sprint", KeyCode::LeftShift);

        // 默认 FPS 轴向映射
        mapping->addAxisMapping("MoveForward", KeyCode::W, 1.0f);
        mapping->addAxisMapping("MoveForward", KeyCode::S, -1.0f);
        mapping->addAxisMapping("MoveRight", KeyCode::D, 1.0f);
        mapping->addAxisMapping("MoveRight", KeyCode::A, -1.0f);
        mapping->addAxisMapping("LookYaw", MouseAxis::MouseX, 1.0f);
        mapping->addAxisMapping("LookPitch", MouseAxis::MouseY, 1.0f);
        mapping->addAxisMapping("Zoom", MouseAxis::MouseWheel, 1.0f);

        return mapping;
    }

    namespace {
        boost::json::object serializeActionMappingsToJson(const std::unordered_map<std::string, ActionMapping>& actionMappings) {
            boost::json::object actionsObj;
            for (const auto& [name, mapping] : actionMappings) {
                boost::json::array keysArr;
                for (KeyCode key : mapping.keys) {
                    keysArr.push_back(boost::json::string(keyCodeToString(key)));
                }
                actionsObj[name] = keysArr;
            }
            return actionsObj;
        }

        boost::json::object serializeAxisMappingsToJson(const std::unordered_map<std::string, AxisMapping>& axisMappings) {
            boost::json::object axesObj;
            for (const auto& [name, mapping] : axisMappings) {
                boost::json::object axisObj;

                boost::json::array keysArr;
                for (const auto& binding : mapping.keyBindings) {
                    boost::json::object kObj;
                    kObj["key"] = keyCodeToString(binding.key);
                    kObj["scale"] = binding.scale;
                    keysArr.push_back(kObj);
                }
                axisObj["keys"] = keysArr;

                boost::json::array mouseArr;
                for (const auto& binding : mapping.mouseBindings) {
                    boost::json::object mObj;
                    mObj["axis"] = mouseAxisToString(binding.axis);
                    mObj["scale"] = binding.scale;
                    mouseArr.push_back(mObj);
                }
                axisObj["mouse"] = mouseArr;

                axisObj["deadzone"] = mapping.deadzone;
                axisObj["clamp"] = mapping.clamp;
                axisObj["clampMin"] = mapping.clampMin;
                axisObj["clampMax"] = mapping.clampMax;
                axisObj["sensitivity"] = mapping.sensitivity;

                axesObj[name] = axisObj;
            }
            return axesObj;
        }

        void deserializeActionMappingsFromJson(const boost::json::object& actionsObj, InputMapping* target) {
            for (const auto& [actName, keysVal] : actionsObj) {
                std::string name(actName);
                if (keysVal.is_array()) {
                    for (const auto& kVal : keysVal.as_array()) {
                        if (kVal.is_string()) {
                            KeyCode key = stringToKeyCode(std::string(kVal.as_string()));
                            if (key != KeyCode::Unknown) {
                                target->addActionMapping(name, key);
                            }
                        }
                    }
                }
            }
        }

        void deserializeAxisMappingsFromJson(const boost::json::object& axesObj, InputMapping* target) {
            for (const auto& [axName, axisVal] : axesObj) {
                std::string name(axName);
                if (axisVal.is_object()) {
                    const auto& aObj = axisVal.as_object();
                    if (aObj.contains("keys") && aObj.at("keys").is_array()) {
                        for (const auto& kVal : aObj.at("keys").as_array()) {
                            if (kVal.is_object()) {
                                const auto& kObj = kVal.as_object();
                                KeyCode key = KeyCode::Unknown;
                                float scale = 1.0f;
                                if (kObj.contains("key") && kObj.at("key").is_string()) {
                                    key = stringToKeyCode(std::string(kObj.at("key").as_string()));
                                }
                                if (kObj.contains("scale") && kObj.at("scale").is_number()) {
                                    scale = static_cast<float>(kObj.at("scale").to_number<double>());
                                }
                                if (key != KeyCode::Unknown) {
                                    target->addAxisMapping(name, key, scale);
                                }
                            }
                        }
                    }
                    if (aObj.contains("mouse") && aObj.at("mouse").is_array()) {
                        for (const auto& mVal : aObj.at("mouse").as_array()) {
                            if (mVal.is_object()) {
                                const auto& mObj = mVal.as_object();
                                MouseAxis axis = MouseAxis::None;
                                float scale = 1.0f;
                                if (mObj.contains("axis") && mObj.at("axis").is_string()) {
                                    axis = stringToMouseAxis(std::string(mObj.at("axis").as_string()));
                                }
                                if (mObj.contains("scale") && mObj.at("scale").is_number()) {
                                    scale = static_cast<float>(mObj.at("scale").to_number<double>());
                                }
                                if (axis != MouseAxis::None) {
                                    target->addAxisMapping(name, axis, scale);
                                }
                            }
                        }
                    }
                    if (aObj.contains("deadzone") && aObj.at("deadzone").is_number()) {
                        target->setAxisDeadzone(name, static_cast<float>(aObj.at("deadzone").to_number<double>()));
                    }
                    if (aObj.contains("clamp") && aObj.at("clamp").is_bool()) {
                        float minVal = -1.0f;
                        float maxVal = 1.0f;
                        if (aObj.contains("clampMin") && aObj.at("clampMin").is_number()) {
                            minVal = static_cast<float>(aObj.at("clampMin").to_number<double>());
                        }
                        if (aObj.contains("clampMax") && aObj.at("clampMax").is_number()) {
                            maxVal = static_cast<float>(aObj.at("clampMax").to_number<double>());
                        }
                        target->setAxisClamp(name, aObj.at("clamp").as_bool(), minVal, maxVal);
                    }
                    if (aObj.contains("sensitivity") && aObj.at("sensitivity").is_number()) {
                        target->setAxisSensitivity(name, static_cast<float>(aObj.at("sensitivity").to_number<double>()));
                    }
                }
            }
        }
    } // namespace

    std::string InputMapping::serializeToJson() const {
        boost::json::object root;
        root["actions"] = serializeActionMappingsToJson(m_actionMappings);
        root["axes"] = serializeAxisMappingsToJson(m_axisMappings);
        return boost::json::serialize(root);
    }

    Status InputMapping::deserializeFromJson(const std::string& jsonString) {
        boost::system::error_code ec;
        boost::json::value val = boost::json::parse(jsonString, ec);
        if (ec) {
            return InvalidArgumentError("Failed to parse input mapping JSON: " + ec.message());
        }
        if (!val.is_object()) {
            return InvalidArgumentError("Input mapping root is not a JSON object");
        }

        const auto& root = val.as_object();
        m_actionMappings.clear();
        m_axisMappings.clear();

        if (root.contains("actions") && root.at("actions").is_object()) {
            deserializeActionMappingsFromJson(root.at("actions").as_object(), this);
        }

        if (root.contains("axes") && root.at("axes").is_object()) {
            deserializeAxisMappingsFromJson(root.at("axes").as_object(), this);
        }

        return OkStatus();
    }

    Status InputMapping::saveToFile(ResourceLoader* loader, const std::string& filePath) const {
        if (!loader) {
            return InvalidArgumentError("ResourceLoader pointer is null");
        }
        std::string jsonStr = serializeToJson();
        return loader->saveFile(filePath, jsonStr);
    }

    Status InputMapping::loadFromFile(ResourceLoader* loader, const std::string& filePath) {
        if (!loader) {
            return InvalidArgumentError("ResourceLoader pointer is null");
        }
        auto contentOr = loader->loadFile(filePath);
        CHECK_STATUS(contentOr);
        return deserializeFromJson(*contentOr);
    }

} // namespace MQEngine
