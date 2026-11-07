#ifndef MQENGINE_KEYCODE_H
#define MQENGINE_KEYCODE_H

#include <cstdint>
#include <string>
#include "../../thirdparty/thirdparty.h"

#ifndef ENGINE_API
#ifdef _WIN32
    #ifdef BUILD_ENGINE
        #define ENGINE_API __declspec(dllexport)
    #else
        #define ENGINE_API __declspec(dllimport)
    #endif
#else
    #define ENGINE_API
#endif
#endif

namespace MQEngine {

    enum class KeyCode : uint16_t {
        Unknown = 0,

        // 鼠标按键 (Mouse Buttons)
        MouseLeft = 1,
        MouseRight = 2,
        MouseMiddle = 4,
        MouseX1 = 5,
        MouseX2 = 6,

        // 控制与编辑键 (Control & Editing Keys)
        Backspace = 8,
        Tab = 9,
        Enter = 13,
        Pause = 19,
        CapsLock = 20,
        Escape = 27,
        Space = 32,
        PageUp = 33,
        PageDown = 34,
        End = 35,
        Home = 36,
        Left = 37,
        Up = 38,
        Right = 39,
        Down = 40,
        PrintScreen = 44,
        Insert = 45,
        Delete = 46,

        // 主键盘数字 ('0' - '9')
        Num0 = 48, Num1 = 49, Num2 = 50, Num3 = 51, Num4 = 52,
        Num5 = 53, Num6 = 54, Num7 = 55, Num8 = 56, Num9 = 57,

        // 字母键 ('A' - 'Z')
        A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71, H = 72,
        I = 73, J = 74, K = 75, L = 76, M = 77, N = 78, O = 79, P = 80,
        Q = 81, R = 82, S = 83, T = 84, U = 85, V = 86, W = 87, X = 88,
        Y = 89, Z = 90,

        // 系统键
        LeftSuper = 91,
        RightSuper = 92,
        Menu = 93,

        // 小键盘数字与符号 (Numpad)
        Numpad0 = 96, Numpad1 = 97, Numpad2 = 98, Numpad3 = 99, Numpad4 = 100,
        Numpad5 = 101, Numpad6 = 102, Numpad7 = 103, Numpad8 = 104, Numpad9 = 105,
        NumpadMultiply = 106,
        NumpadAdd = 107,
        NumpadSeparator = 108,
        NumpadSubtract = 109,
        NumpadDecimal = 110,
        NumpadDivide = 111,

        // 功能键 (F1 - F12)
        F1 = 112, F2 = 113, F3 = 114, F4 = 115, F5 = 116, F6 = 117,
        F7 = 118, F8 = 119, F9 = 120, F10 = 121, F11 = 122, F12 = 123,

        // 锁与修饰符 (Modifiers & Locks)
        NumLock = 144,
        ScrollLock = 145,
        LeftShift = 160,
        RightShift = 161,
        LeftControl = 162,
        RightControl = 163,
        LeftAlt = 164,
        RightAlt = 165,

        // 标点符号 (Punctuation)
        Semicolon = 186,
        Equal = 187,
        Comma = 188,
        Minus = 189,
        Period = 190,
        Slash = 191,
        GraveAccent = 192,
        LeftBracket = 219,
        Backslash = 220,
        RightBracket = 221,
        Apostrophe = 222,

        MaxKeyCount = 512
    };

    enum class MouseButton : uint8_t {
        Left = 0,
        Right = 1,
        Middle = 2,
        X1 = 3,
        X2 = 4,
        Count = 5
    };

    enum class MouseAxis : uint8_t {
        None = 0,
        MouseX = 1,
        MouseY = 2,
        MouseWheel = 3
    };

    using AxisSource = MouseAxis;

    ENGINE_API KeyCode win32VirtualKeyToKeyCode(int vkCode);
    ENGINE_API int keyCodeToWin32VirtualKey(KeyCode code);
    ENGINE_API KeyCode mouseButtonToKeyCode(MouseButton button);
    ENGINE_API MouseButton keyCodeToMouseButton(KeyCode code);
    ENGINE_API bool isMouseKey(KeyCode code);
    ENGINE_API std::string keyCodeToString(KeyCode code);
    ENGINE_API KeyCode stringToKeyCode(const std::string& str);
    ENGINE_API std::string mouseAxisToString(MouseAxis axis);
    ENGINE_API MouseAxis stringToMouseAxis(const std::string& str);

} // namespace MQEngine

#endif // MQENGINE_KEYCODE_H
