/**
 * @file InputMappingContext.cpp
 * @brief 输入映射上下文 (IMC) TOML 解析与 ResourceLoader 加载实现
 */
#include <MQEngine/domain/hid/InputMappingContext.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/thirdparty/TomlBackend.h>

namespace MQEngine::Domain::HID
{
    namespace
    {
        void appendModifierFromName(
            std::string_view modName,
            const toml::table* modTbl,
            ActionBindingMapping& outMapping)
        {
            if (modName == "SwizzleAxis" || modName == "SwizzleYXZ")
            {
                ESwizzleAxisOrder order = ESwizzleAxisOrder::YXZ;
                if (modTbl)
                {
                    const std::string ordStr = (*modTbl)["order"].value_or(std::string{"YXZ"});
                    if (ordStr == "ZYX") order = ESwizzleAxisOrder::ZYX;
                    else if (ordStr == "XZY") order = ESwizzleAxisOrder::XZY;
                }
                outMapping.modifiers.push_back(makeUnique<InputModifierSwizzleAxis>(order));
            }
            else if (modName == "Negate")
            {
                bool nx = true;
                bool ny = true;
                bool nz = true;
                if (modTbl)
                {
                    nx = (*modTbl)["x"].value_or(true);
                    ny = (*modTbl)["y"].value_or(true);
                    nz = (*modTbl)["z"].value_or(true);
                }
                outMapping.modifiers.push_back(makeUnique<InputModifierNegate>(nx, ny, nz));
            }
            else if (modName == "DeadZone")
            {
                const float threshold = modTbl
                    ? static_cast<float>((*modTbl)["threshold"].value_or(0.2))
                    : 0.2f;
                outMapping.modifiers.push_back(makeUnique<InputModifierDeadZone>(threshold));
            }
            else if (modName == "Normalize2D")
            {
                outMapping.modifiers.push_back(makeUnique<InputModifierNormalize2D>());
            }
            else if (modName == "Scalar")
            {
                const float factor = modTbl
                    ? static_cast<float>((*modTbl)["factor"].value_or(1.0))
                    : 1.0f;
                outMapping.modifiers.push_back(makeUnique<InputModifierScalar>(factor));
            }
            else if (modName == "DeltaTimeScale")
            {
                outMapping.modifiers.push_back(makeUnique<InputModifierDeltaTimeScale>());
            }
        }

        void appendTriggerFromName(
            std::string_view trigName,
            const toml::table* trigTbl,
            ActionBindingMapping& outMapping)
        {
            if (trigName == "Down")
            {
                outMapping.triggers.push_back(makeUnique<InputTriggerDown>());
            }
            else if (trigName == "Pressed")
            {
                outMapping.triggers.push_back(makeUnique<InputTriggerPressed>());
            }
            else if (trigName == "Released")
            {
                outMapping.triggers.push_back(makeUnique<InputTriggerReleased>());
            }
            else if (trigName == "Tap")
            {
                const float threshold = trigTbl
                    ? static_cast<float>((*trigTbl)["threshold"].value_or(0.20))
                    : 0.20f;
                outMapping.triggers.push_back(makeUnique<InputTriggerTap>(threshold));
            }
            else if (trigName == "Hold")
            {
                const float threshold = trigTbl
                    ? static_cast<float>((*trigTbl)["threshold"].value_or(0.40))
                    : 0.40f;
                const bool oneShot = trigTbl ? (*trigTbl)["one_shot"].value_or(false) : false;
                outMapping.triggers.push_back(makeUnique<InputTriggerHold>(threshold, oneShot));
            }
            else if (trigName == "HoldAndRelease")
            {
                const float threshold = trigTbl
                    ? static_cast<float>((*trigTbl)["threshold"].value_or(0.50))
                    : 0.50f;
                outMapping.triggers.push_back(makeUnique<InputTriggerHoldAndRelease>(threshold));
            }
            else if (trigName == "ChordedAction" || trigName == "Chord")
            {
                const std::string chordAction = trigTbl
                    ? (*trigTbl)["chord_action"].value_or(std::string{})
                    : std::string{};
                outMapping.triggers.push_back(makeUnique<InputTriggerChordedAction>(0u, chordAction));
            }
        }
    } // namespace

    void InputMappingContext::addBinding(ActionBindingMapping mapping)
    {
        m_bindings.push_back(std::move(mapping));
    }

    Status InputMappingContext::loadFromToml(std::string_view tomlContent)
    {
        if (tomlContent.empty())
        {
            return InvalidArgumentError("IMC TOML 内容不能为空");
        }

        toml::parse_result result = toml::parse(tomlContent);
        if (result.failed())
        {
            return InvalidArgumentError(StrCat("IMC TOML 解析失败: ", result.error().description()));
        }

        m_bindings.clear();
        const auto& tbl = result.table();

        if (const auto* ctxTbl = tbl["context"].as_table())
        {
            m_name = (*ctxTbl)["name"].value_or(std::string{m_name});
            m_priority = static_cast<int32_t>((*ctxTbl)["priority"].value_or(static_cast<int64_t>(m_priority)));
        }
        else
        {
            m_name = tbl["name"].value_or(std::string{m_name});
            m_priority = static_cast<int32_t>(tbl["priority"].value_or(static_cast<int64_t>(m_priority)));
        }

        if (const auto* bindingsArr = tbl["bindings"].as_array())
        {
            for (const auto& elem : *bindingsArr)
            {
                const auto* bTbl = elem.as_table();
                if (!bTbl)
                {
                    continue;
                }

                ActionBindingMapping mapping;
                mapping.inputSourceKey = (*bTbl)["source"].value_or((*bTbl)["key"].value_or(std::string{}));
                mapping.actionName = (*bTbl)["action"].value_or(std::string{});
                mapping.consumesInput = (*bTbl)["consumes_input"].value_or(true);

                if (const auto* modsArr = (*bTbl)["modifiers"].as_array())
                {
                    for (const auto& modElem : *modsArr)
                    {
                        if (auto modName = modElem.value<std::string>())
                        {
                            appendModifierFromName(*modName, nullptr, mapping);
                        }
                        else if (const auto* modTbl = modElem.as_table())
                        {
                            const std::string tName = (*modTbl)["type"].value_or(std::string{});
                            appendModifierFromName(tName, modTbl, mapping);
                        }
                    }
                }

                if (const auto* trigArr = (*bTbl)["triggers"].as_array())
                {
                    for (const auto& trigElem : *trigArr)
                    {
                        if (auto trigName = trigElem.value<std::string>())
                        {
                            appendTriggerFromName(*trigName, nullptr, mapping);
                        }
                        else if (const auto* trigTbl = trigElem.as_table())
                        {
                            const std::string tName = (*trigTbl)["type"].value_or(std::string{});
                            appendTriggerFromName(tName, trigTbl, mapping);
                        }
                    }
                }

                m_bindings.push_back(std::move(mapping));
            }
        }

        return OkStatus();
    }

    Status InputMappingContext::loadFromFile(std::string_view assetPath)
    {
        if (assetPath.empty())
        {
            return InvalidArgumentError("IMC 资产路径不能为空");
        }

        if (auto* resMod = ::MQEngine::Resource::Resource::InstancePtr())
        {
            if (auto* loader = resMod->getResourceLoader())
            {
                if (loader->exists(std::string(assetPath)))
                {
                    auto textRes = loader->loadFile(std::string(assetPath));
                    CHECK_STATUS(textRes);
                    return loadFromToml(*textRes);
                }
            }
        }

        if (FCT::SAI::FileSystem::exists(assetPath))
        {
            auto textRes = FCT::SAI::FileSystem::readTextFile(assetPath);
            CHECK_STATUS(textRes);
            return loadFromToml(*textRes);
        }

        return NotFoundError(StrCat("未找到 IMC 配置文件: ", assetPath));
    }
}
