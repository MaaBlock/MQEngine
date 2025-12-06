
#ifndef SHADERSNIPPETMANAGER_H
#define SHADERSNIPPETMANAGER_H
#include <list>
#include "../thirdparty/thirdparty.h"
#include "../EnginePCH.h"
#include <boost/serialization/access.hpp>
#include <unordered_map>
#include <functional>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/tag.hpp>

namespace MQEngine
{
    struct SnippetMeta {
        std::string uuid;
        size_t sourceHash;

        template<class Archive>
        void serialize(Archive & ar, const unsigned int version) {
            ar & uuid & sourceHash;
        }
    };

    enum class ParamDirection {
        In,
        Out,
        InOut
    };

    struct ShaderParameter {
        ParamDirection direction;
        std::string type;
        std::string name;
    };
    struct Snippet
    {
        std::string uuid;
        std::string name;
        size_t sourceHash;
        std::string source;
        std::vector<ShaderParameter> inputs;
        std::vector<ShaderParameter> outputs;
        std::vector<ShaderParameter> inouts;
        bool isEmbed;
        std::string filePath;
    };

    struct ByUuid {};
    struct ByName {};
    struct ByHash {};

    using SnippetContainer = boost::multi_index_container<
        Snippet,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<boost::multi_index::tag<ByUuid>, boost::multi_index::member<Snippet, std::string, &Snippet::uuid>>,
            boost::multi_index::hashed_unique<boost::multi_index::tag<ByName>, boost::multi_index::member<Snippet, std::string, &Snippet::name>>,
            boost::multi_index::hashed_non_unique<boost::multi_index::tag<ByHash>, boost::multi_index::member<Snippet, size_t, &Snippet::sourceHash>>
        >
    >;

    namespace SnippetEvent {
        struct Create {
            std::string name;
            std::string uuid;
        };
        struct Update {
            std::string name;
            std::string uuid;
        };
        struct Rename {
            std::string oldName;
            std::string newName;
            std::string uuid;
        };
    }

    class ENGINE_API ShaderSnippetManager
    {
    public:
        ShaderSnippetManager();
        ~ShaderSnippetManager();
        ShaderSnippetManager(const ShaderSnippetManager&) = delete;
        ShaderSnippetManager& operator=(const ShaderSnippetManager&) = delete;
        ShaderSnippetManager(ShaderSnippetManager&&) = delete;
        ShaderSnippetManager& operator=(ShaderSnippetManager&&) = delete;

        bool hasMetaByUuid(const std::string& uuid) const;
        bool hasMetaByName(const std::string& name) const;
        bool hasMetaByHash(size_t hash) const;

        Status loadSnippetFromResource();
        Status loadSnippet(const std::string& snippetName);
        Status unloadSnippet(const std::string& snippetName);
        Status registerSnippet(
            const std::string& uuid,
            const std::string& snippetName,
            const std::string& snippetSource);
        const Snippet* getSnippetByName(const std::string& snippetName) const;
        const Snippet* getSnippetByUuid(const std::string& uuid) const;
        const SnippetContainer& getSnippets() const;

        template<typename Event, typename Func>
        FCT::SubscribeId subscribe(Func&& func) {
            return m_dispatcher.subscribe<Event>(std::forward<Func>(func));
        }

        template<typename Event>
        void unsubscribe(FCT::SubscribeId subscribeId) {
            m_dispatcher.unsubscribe<Event>(subscribeId);
        }

    private:
        template<typename Event>
        void trigger(const Event& event) {
            m_dispatcher.trigger(event);
        }

        static StatusOr<std::vector<ShaderParameter>> parseParameters(std::string_view source);
        Status registerSnippet(const std::string& uuid, const std::string& snippetName,
                               const std::string& snippetSource, bool isEmbed, const std::string& filePath);
        
        SnippetContainer m_snippets;
        FCT::Shaderc_ShaderCompiler m_compiler;
        FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly> m_dispatcher;
    };
} // MQEngine

#endif //SHADERSNIPPETMANAGER_H
