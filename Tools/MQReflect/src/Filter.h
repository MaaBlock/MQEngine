#pragma once

#include "AST.h"

namespace MQReflect {

class Filter {
public:
    static bool isRecordEligible(const RecordAST& record, bool autoReflectMode);
    static bool isFieldEligible(const FieldAST& field);
    static bool isMethodEligible(const MethodAST& method);
    static bool isEnumEligible(const EnumAST& enumAst, bool autoReflectMode);
};

} // namespace MQReflect
