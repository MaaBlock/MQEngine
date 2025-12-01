#ifndef CONTENTBROWSER_H
#define CONTENTBROWSER_H

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <tuple>
#include "../thirdparty/thirdparty.h"

namespace MQEngine {
    
    class IContentProvider {
    public:
        virtual ~IContentProvider() = default;
        virtual void init() {}
        virtual void render() = 0;
        virtual void term() {}
        virtual std::string getName() const = 0;
    };

    class ContentBrowser {
    public:
        ~ContentBrowser() {
            for (auto& provider : m_providers) {
                provider->term();
            }
            m_providers.clear();
        }

        template <typename T, typename... Args>
        void registerProvider(Args&&... args) {
            auto provider = FCT::makeUnique<T>(std::forward<Args>(args)...);
            provider->init();
            m_providers.push_back(std::move(provider));
        }

        template <typename... Ts>
        void registerAll() {
            (registerProvider<Ts>(), ...);
        }

        template <typename Tuple>
        void registerProvidersTuple() {
            registerProvidersTupleImpl<Tuple>(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
        }

        void render();

    private:
        template <typename Tuple, size_t... Is>
        void registerProvidersTupleImpl(std::index_sequence<Is...>) {
            (registerProvider<std::tuple_element_t<Is, Tuple>>(), ...);
        }

        std::vector<FCT::UniquePtr<IContentProvider>> m_providers;
        std::string m_selectedContentName;
    };
}

#endif // CONTENTBROWSER_H
