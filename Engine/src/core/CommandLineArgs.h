#ifndef ENGINE_COMMANDLINEARGS_H
#define ENGINE_COMMANDLINEARGS_H

#include <string>
#include <vector>
#include <unordered_map>
#include "../EnginePCH.h"

namespace MQEngine {

/**
 * @brief 引擎全局命令行参数解析与存储单例
 */
class ENGINE_API CommandLineArgs {
public:
    static CommandLineArgs& instance() {
        static CommandLineArgs s_instance;
        return s_instance;
    }

    void parse(int argc, char* argv[]) {
        std::vector<std::string> args;
        args.reserve(static_cast<size_t>(argc));
        for (int i = 0; i < argc; ++i) {
            if (argv[i]) {
                args.emplace_back(argv[i]);
            }
        }
        parse(args);
    }

    void parse(const std::vector<std::string>& args) {
        m_rawArgs = args;
        for (size_t i = 1; i < args.size(); ++i) {
            parseSingleArg(args, i);
        }
    }

    bool hasProject() const { return !m_projectPath.empty(); }
    const std::string& getProjectPath() const { return m_projectPath; }
    void setProjectPath(const std::string& path) { m_projectPath = path; }

    bool isHeadless() const { return m_headless; }
    void setHeadless(bool headless) { m_headless = headless; }

    bool hasOption(const std::string& key) const {
        return m_options.find(key) != m_options.end();
    }

    std::string getOption(const std::string& key, const std::string& defaultVal = "") const {
        auto it = m_options.find(key);
        if (it != m_options.end()) {
            return it->second;
        }
        return defaultVal;
    }

    void setOption(const std::string& key, const std::string& value) {
        m_options[key] = value;
    }

    const std::vector<std::string>& getRawArgs() const { return m_rawArgs; }

private:
    CommandLineArgs() = default;

    void parseSingleArg(const std::vector<std::string>& args, size_t& i) {
        const std::string& arg = args[i];

        if (arg == "--project" || arg == "-p") {
            if (i + 1 < args.size()) {
                m_projectPath = args[++i];
            }
            return;
        }

        if (arg.rfind("--project=", 0) == 0) {
            m_projectPath = arg.substr(10);
            return;
        }

        if (arg == "--headless" || arg == "-h") {
            m_headless = true;
            m_options["headless"] = "true";
            return;
        }

        parseGenericOption(args, i, arg);
    }

    void parseGenericOption(const std::vector<std::string>& args, size_t& i, const std::string& arg) {
        if (arg.rfind("--", 0) != 0) {
            return;
        }

        std::string optionBody = arg.substr(2);
        size_t eqPos = optionBody.find('=');
        if (eqPos != std::string::npos) {
            std::string key = optionBody.substr(0, eqPos);
            std::string val = optionBody.substr(eqPos + 1);
            m_options[key] = val;
            return;
        }

        if (i + 1 < args.size() && args[i + 1].rfind("-", 0) != 0) {
            m_options[optionBody] = args[++i];
        } else {
            m_options[optionBody] = "true";
        }
    }

    std::string m_projectPath;
    bool m_headless = false;
    std::unordered_map<std::string, std::string> m_options;
    std::vector<std::string> m_rawArgs;
};

} // namespace MQEngine

#endif // ENGINE_COMMANDLINEARGS_H
