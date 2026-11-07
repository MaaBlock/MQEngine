#include "KeyCode.h"
#include <unordered_map>
#include <algorithm>

namespace MQEngine {

    namespace {
        const std::unordered_map<KeyCode, std::string>& getKeyCodeToStringMap() {
            static const std::unordered_map<KeyCode, std::string> s_map = {
                {KeyCode::Unknown, "Unknown"},
                {KeyCode::MouseLeft, "MouseLeft"},
                {KeyCode::MouseRight, "MouseRight"},
                {KeyCode::MouseMiddle, "MouseMiddle"},
                {KeyCode::MouseX1, "MouseX1"},
                {KeyCode::MouseX2, "MouseX2"},
                {KeyCode::Backspace, "Backspace"},
                {KeyCode::Tab, "Tab"},
                {KeyCode::Enter, "Enter"},
                {KeyCode::Pause, "Pause"},
                {KeyCode::CapsLock, "CapsLock"},
                {KeyCode::Escape, "Escape"},
                {KeyCode::Space, "Space"},
                {KeyCode::PageUp, "PageUp"},
                {KeyCode::PageDown, "PageDown"},
                {KeyCode::End, "End"},
                {KeyCode::Home, "Home"},
                {KeyCode::Left, "Left"},
                {KeyCode::Up, "Up"},
                {KeyCode::Right, "Right"},
                {KeyCode::Down, "Down"},
                {KeyCode::PrintScreen, "PrintScreen"},
                {KeyCode::Insert, "Insert"},
                {KeyCode::Delete, "Delete"},
                {KeyCode::Num0, "0"},
                {KeyCode::Num1, "1"},
                {KeyCode::Num2, "2"},
                {KeyCode::Num3, "3"},
                {KeyCode::Num4, "4"},
                {KeyCode::Num5, "5"},
                {KeyCode::Num6, "6"},
                {KeyCode::Num7, "7"},
                {KeyCode::Num8, "8"},
                {KeyCode::Num9, "9"},
                {KeyCode::A, "A"},
                {KeyCode::B, "B"},
                {KeyCode::C, "C"},
                {KeyCode::D, "D"},
                {KeyCode::E, "E"},
                {KeyCode::F, "F"},
                {KeyCode::G, "G"},
                {KeyCode::H, "H"},
                {KeyCode::I, "I"},
                {KeyCode::J, "J"},
                {KeyCode::K, "K"},
                {KeyCode::L, "L"},
                {KeyCode::M, "M"},
                {KeyCode::N, "N"},
                {KeyCode::O, "O"},
                {KeyCode::P, "P"},
                {KeyCode::Q, "Q"},
                {KeyCode::R, "R"},
                {KeyCode::S, "S"},
                {KeyCode::T, "T"},
                {KeyCode::U, "U"},
                {KeyCode::V, "V"},
                {KeyCode::W, "W"},
                {KeyCode::X, "X"},
                {KeyCode::Y, "Y"},
                {KeyCode::Z, "Z"},
                {KeyCode::LeftSuper, "LeftSuper"},
                {KeyCode::RightSuper, "RightSuper"},
                {KeyCode::Menu, "Menu"},
                {KeyCode::Numpad0, "Numpad0"},
                {KeyCode::Numpad1, "Numpad1"},
                {KeyCode::Numpad2, "Numpad2"},
                {KeyCode::Numpad3, "Numpad3"},
                {KeyCode::Numpad4, "Numpad4"},
                {KeyCode::Numpad5, "Numpad5"},
                {KeyCode::Numpad6, "Numpad6"},
                {KeyCode::Numpad7, "Numpad7"},
                {KeyCode::Numpad8, "Numpad8"},
                {KeyCode::Numpad9, "Numpad9"},
                {KeyCode::NumpadMultiply, "NumpadMultiply"},
                {KeyCode::NumpadAdd, "NumpadAdd"},
                {KeyCode::NumpadSeparator, "NumpadSeparator"},
                {KeyCode::NumpadSubtract, "NumpadSubtract"},
                {KeyCode::NumpadDecimal, "NumpadDecimal"},
                {KeyCode::NumpadDivide, "NumpadDivide"},
                {KeyCode::F1, "F1"},
                {KeyCode::F2, "F2"},
                {KeyCode::F3, "F3"},
                {KeyCode::F4, "F4"},
                {KeyCode::F5, "F5"},
                {KeyCode::F6, "F6"},
                {KeyCode::F7, "F7"},
                {KeyCode::F8, "F8"},
                {KeyCode::F9, "F9"},
                {KeyCode::F10, "F10"},
                {KeyCode::F11, "F11"},
                {KeyCode::F12, "F12"},
                {KeyCode::NumLock, "NumLock"},
                {KeyCode::ScrollLock, "ScrollLock"},
                {KeyCode::LeftShift, "LeftShift"},
                {KeyCode::RightShift, "RightShift"},
                {KeyCode::LeftControl, "LeftControl"},
                {KeyCode::RightControl, "RightControl"},
                {KeyCode::LeftAlt, "LeftAlt"},
                {KeyCode::RightAlt, "RightAlt"},
                {KeyCode::Semicolon, "Semicolon"},
                {KeyCode::Equal, "Equal"},
                {KeyCode::Comma, "Comma"},
                {KeyCode::Minus, "Minus"},
                {KeyCode::Period, "Period"},
                {KeyCode::Slash, "Slash"},
                {KeyCode::GraveAccent, "GraveAccent"},
                {KeyCode::LeftBracket, "LeftBracket"},
                {KeyCode::Backslash, "Backslash"},
                {KeyCode::RightBracket, "RightBracket"},
                {KeyCode::Apostrophe, "Apostrophe"}
            };
            return s_map;
        }

        const std::unordered_map<std::string, KeyCode>& getStringToKeyCodeMap() {
            static std::unordered_map<std::string, KeyCode> s_map;
            static bool s_inited = false;
            if (!s_inited) {
                for (const auto& pair : getKeyCodeToStringMap()) {
                    std::string lower = pair.second;
                    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                        return static_cast<char>(std::tolower(c));
                    });
                    s_map[pair.second] = pair.first;
                    s_map[lower] = pair.first;
                }
                s_inited = true;
            }
            return s_map;
        }
    } // namespace

    KeyCode win32VirtualKeyToKeyCode(int vkCode) {
        if (vkCode < 0 || vkCode >= static_cast<int>(KeyCode::MaxKeyCount)) {
            return KeyCode::Unknown;
        }
        return static_cast<KeyCode>(vkCode);
    }

    int keyCodeToWin32VirtualKey(KeyCode code) {
        return static_cast<int>(code);
    }

    KeyCode mouseButtonToKeyCode(MouseButton button) {
        switch (button) {
            case MouseButton::Left:   return KeyCode::MouseLeft;
            case MouseButton::Right:  return KeyCode::MouseRight;
            case MouseButton::Middle: return KeyCode::MouseMiddle;
            case MouseButton::X1:     return KeyCode::MouseX1;
            case MouseButton::X2:     return KeyCode::MouseX2;
            default:                  return KeyCode::Unknown;
        }
    }

    MouseButton keyCodeToMouseButton(KeyCode code) {
        switch (code) {
            case KeyCode::MouseLeft:   return MouseButton::Left;
            case KeyCode::MouseRight:  return MouseButton::Right;
            case KeyCode::MouseMiddle: return MouseButton::Middle;
            case KeyCode::MouseX1:     return MouseButton::X1;
            case KeyCode::MouseX2:     return MouseButton::X2;
            default:                   return MouseButton::Left;
        }
    }

    bool isMouseKey(KeyCode code) {
        return code == KeyCode::MouseLeft ||
               code == KeyCode::MouseRight ||
               code == KeyCode::MouseMiddle ||
               code == KeyCode::MouseX1 ||
               code == KeyCode::MouseX2;
    }

    std::string keyCodeToString(KeyCode code) {
        const auto& map = getKeyCodeToStringMap();
        auto it = map.find(code);
        if (it != map.end()) {
            return it->second;
        }
        return "Unknown";
    }

    KeyCode stringToKeyCode(const std::string& str) {
        const auto& map = getStringToKeyCodeMap();
        auto it = map.find(str);
        if (it != map.end()) {
            return it->second;
        }
        std::string lower = str;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        it = map.find(lower);
        if (it != map.end()) {
            return it->second;
        }
        return KeyCode::Unknown;
    }

    std::string mouseAxisToString(MouseAxis axis) {
        switch (axis) {
            case MouseAxis::MouseX:     return "MouseX";
            case MouseAxis::MouseY:     return "MouseY";
            case MouseAxis::MouseWheel: return "MouseWheel";
            default:                    return "None";
        }
    }

    MouseAxis stringToMouseAxis(const std::string& str) {
        if (str == "MouseX" || str == "mousex") return MouseAxis::MouseX;
        if (str == "MouseY" || str == "mousey") return MouseAxis::MouseY;
        if (str == "MouseWheel" || str == "mousewheel") return MouseAxis::MouseWheel;
        return MouseAxis::None;
    }

} // namespace MQEngine
