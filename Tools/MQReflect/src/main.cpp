#include "Lexer.h"
#include "Parser.h"
#include "Generator.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "MQReflect - MQEngine 静态编译期反射预处理器\n"
                  << "用法: mq_reflect [-o <输出文件>] [--auto-reflect] <头文件路径...>\n";
        return 0;
    }

    std::filesystem::path outputPath = "reflection.gen.h";
    bool autoReflectMode = false;
    std::vector<std::filesystem::path> inputHeaders;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (arg == "--auto-reflect") {
            autoReflectMode = true;
        } else if (arg == "-i" && i + 1 < argc) {
            inputHeaders.emplace_back(argv[++i]);
        } else if (!arg.empty() && arg[0] != '-') {
            inputHeaders.emplace_back(arg);
        }
    }

    if (inputHeaders.empty()) {
        std::cerr << "[MQReflect] 错误: 未指定任何待反射的输入头文件。\n";
        return 1;
    }

    assert(!inputHeaders.empty() && "待反射输入头文件列表不能为空");

    std::vector<MQReflect::ParsedHeaderAST> parsedHeaders;
    for (const auto& headerPath : inputHeaders) {
        if (!std::filesystem::exists(headerPath)) {
            std::cerr << "[MQReflect] 警告: 头文件不存在，跳过: " << headerPath.string() << std::endl;
            continue;
        }

        std::ifstream file(headerPath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[MQReflect] 错误: 无法读取头文件: " << headerPath.string() << std::endl;
            return 1;
        }

        std::string source((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

        MQReflect::Lexer lexer(source, headerPath.string());
        std::vector<MQReflect::Token> tokens = lexer.tokenize();

        MQReflect::Parser parser(std::move(tokens), headerPath.string());
        parsedHeaders.push_back(parser.parse());
    }

    std::string generatedCode = MQReflect::Generator::generateHeader(parsedHeaders, autoReflectMode);

    if (!MQReflect::Generator::writeIfChanged(outputPath, generatedCode)) {
        std::cerr << "[MQReflect] 错误: 写入输出文件失败: " << outputPath.string() << std::endl;
        return 1;
    }

    std::cout << "[MQReflect] 成功生成反射元数据: " << outputPath.string() << std::endl;
    return 0;
}
