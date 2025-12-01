#ifndef SHADERSNIPPETMANAGER_H
#define SHADERSNIPPETMANAGER_H
#include "../thirdparty/thirdparty.h"
#include "../EnginePCH.h"
namespace MQEngine
{
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
        std::string source;
        std::vector<ShaderParameter> inputs;
        std::vector<ShaderParameter> outputs;
        std::vector<ShaderParameter> inouts;
    };


    class ENGINE_API ShaderSnippetManager {
    public:
        Status registerSnippet(const std::string& uuid, const std::string& snippetName, const std::string& snippetSource);
        static StatusOr<std::vector<ShaderParameter>> parseParameters(std::string_view source);
        
        const Snippet* getSnippetByName(const std::string& snippetName) const;
        const Snippet* getSnippetByUuid(const std::string& uuid) const;
        const std::unordered_map<std::string, Snippet>& getSnippets() const { return m_snippets; }

    private:
        std::unordered_map<std::string, Snippet> m_snippets; // Key is UUID now
        FCT::Shaderc_ShaderCompiler m_compiler;
    };
} // MQEngine

#endif //SHADERSNIPPETMANAGER_H
