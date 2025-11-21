#ifndef GENERICINPUTPOPUP_H
#define GENERICINPUTPOPUP_H

#include "../thirdparty/thirdparty.h"
#include <string>
#include <functional>

namespace MQEngine {

    class GenericInputPopup {
    public:
        void open(const std::string& title, const std::string& label, const std::string& defaultValue,
                  std::function<void(const std::string&)> onConfirm, const std::string& hintText = "");
        void render();

    private:
        bool m_isOpen = false;
        bool m_shouldOpen = false;
        std::string m_title;
        std::string m_label;
        std::string m_hintText;
        char m_buffer[512];
        std::function<void(const std::string&)> m_onConfirm;
    };

} // MQEngine

#endif //GENERICINPUTPOPUP_H
