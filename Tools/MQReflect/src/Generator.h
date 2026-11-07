#pragma once

#include "AST.h"
#include <string>
#include <filesystem>
#include <vector>

namespace MQReflect {

class Generator {
public:
    static std::string generateHeader(const std::vector<ParsedHeaderAST>& headers, bool autoReflectMode);
    static bool writeIfChanged(const std::filesystem::path& outputPath, const std::string& newContent);

private:
    static void generateRecordMeta(std::string& out, const RecordAST& record, bool autoReflectMode);
    static void generateEnumMeta(std::string& out, const EnumAST& enumAst, bool autoReflectMode);
};

} // namespace MQReflect
