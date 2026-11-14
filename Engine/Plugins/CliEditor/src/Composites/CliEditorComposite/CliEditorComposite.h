/**
 * @file CliEditorComposite.h
 * @brief 渐进式披露 CLI 与 AI 自动化编辑器核心 Composite
 */
#ifndef MQENGINE_PLUGIN_CLIEDITOR_CLIEDITORCOMPOSITE_H
#define MQENGINE_PLUGIN_CLIEDITOR_CLIEDITORCOMPOSITE_H

#include "CliEditorTypes.h"
#include <MQEngine/asset/interface/Composite.h>

namespace MQEngine
{
    namespace CliEditor
    {
        class CliEditorComposite : public Asset::Composite
        {
        public:
            CliEditorComposite();
            ~CliEditorComposite() override = default;

            std::string getName() const override { return "CliEditorComposite"; }
            std::string getPluginName() const override { return "CliEditor"; }
            std::vector<std::string> getRequiredTags() const override { return {"Runtime", "CliEditor"}; }
            std::vector<std::string> getDependencies() const override { return {}; }

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            void registerDomain(CliDomainSpec domainSpec);
            void registerCommand(CliCommandSpec commandSpec);

            std::string formatLevel0Domains(bool asJson) const;
            std::string formatLevel1DomainCommands(const std::string& domain, bool asJson) const;
            std::string formatLevel2CommandSchema(const std::string& fullCommandName, bool asJson) const;

            CliExecutionResult executeCommandLine(const std::string& commandLine);
            std::vector<CliExecutionResult> executePipeline(const std::string& pipelineExpr);

            void requestDeferredExitAfterFrames(uint32_t framesToWait) noexcept
            {
                m_waitRenderFramesBeforeExit = std::max(m_waitRenderFramesBeforeExit, framesToWait);
            }

        private:
            void registerAllDomainsAndCommands();
            void registerSceneAndCharacterCommands();
            void registerRenderAndShaderCommands();
            void registerAssetAndWorldCommands();

            void parseCommandLineArguments();
            void executeStartupCliWorkload();
            void emitOutput(const std::string& text) const;

            static std::vector<std::string> tokenizeCommandExpr(const std::string& expr);
            static std::unordered_map<std::string, std::string> parseKeyValueArgs(
                const std::vector<std::string>& tokens,
                size_t startIndex,
                const CliCommandSpec& spec);

        private:
            Runtime::Engine* m_engine = nullptr;
            std::vector<CliDomainSpec> m_domains;
            std::vector<CliCommandSpec> m_commands;

            std::string m_cliHelpDomain;
            std::string m_cliSchemaTarget;
            std::string m_cliPipelineExpr;
            std::string m_cliScriptPath;
            std::string m_cliOutputPath;
            bool m_cliRequestedHelp = false;
            bool m_cliJsonMode = false;
            bool m_hasExecutedStartupWorkload = false;
            uint32_t m_tickFrameCounter = 0;
            uint32_t m_waitRenderFramesBeforeExit = 0;
        };

    } // namespace CliEditor
} // namespace MQEngine

#endif // MQENGINE_PLUGIN_CLIEDITOR_CLIEDITORCOMPOSITE_H
