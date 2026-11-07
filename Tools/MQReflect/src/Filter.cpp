#include "Filter.h"

namespace MQReflect {

bool Filter::isRecordEligible(const RecordAST& record, bool autoReflectMode) {
    if (record.isForwardDecl) return false; // 跳过前向声明
    if (record.isTemplate) return false;    // 跳过未特化模板
    if (record.attrs.ignore) return false;  // 跳过 [[mq::ignore]]
    if (record.name.empty()) return false;  // 跳过匿名类/结构体

    if (!autoReflectMode) {
        // 选择性反射模式下必须具备显式注解
        return record.attrs.reflect || record.attrs.service || record.attrs.script;
    }
    return true;
}

bool Filter::isFieldEligible(const FieldAST& field) {
    if (field.access != AccessSpecifier::Public) return false; // 仅反射 public 字段
    if (field.attrs.ignore) return false;                      // 跳过 [[mq::ignore]]
    if (field.name.empty()) return false;
    if (field.isStatic) return false;                          // 静态字段无法形成普通成员指针
    if (field.isConstexpr) return false;                       // 编译期静态常量
    if (field.isReference) return false;                       // 引用成员禁止取地址
    if (field.isBitfield) return false;                        // 位域成员禁止取地址
    return true;
}

bool Filter::isMethodEligible(const MethodAST& method) {
    if (method.access != AccessSpecifier::Public) return false; // 仅反射 public 方法
    if (method.attrs.ignore) return false;                      // 跳过 [[mq::ignore]]
    if (method.name.empty()) return false;
    if (method.isConstructor || method.isDestructor) return false; // 跳过构造/析构函数
    if (method.isOperator) return false;                        // 跳过运算符重载
    if (method.isDeleted) return false;                         // 跳过 = delete
    if (method.isTemplate) return false;                        // 跳过成员函数模板
    if (method.isStatic) return false;                          // 静态函数暂不纳入成员函数指针元组
    return true;
}

bool Filter::isEnumEligible(const EnumAST& enumAst, bool autoReflectMode) {
    if (enumAst.isForwardDecl) return false; // 跳过前向声明
    if (enumAst.attrs.ignore) return false;  // 跳过 [[mq::ignore]]
    if (enumAst.name.empty()) return false;  // 跳过匿名枚举
    if (!autoReflectMode && !enumAst.attrs.reflect) return false;
    return true;
}

} // namespace MQReflect
