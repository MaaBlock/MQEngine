#ifndef MQENGINE_RESOURCE_CLASSDEF_H
#define MQENGINE_RESOURCE_CLASSDEF_H

#include <cstdint>

namespace MQEngine
{
    namespace Resource
    {
        class Config;
        struct CommandLineArg;
        class ICommandLineHandler;
        class CommandLineParser;

        // 内存管理
        class MemoryTracker;
        class Arena;
        class LinearArena;
        class DoubleBufferedFrameArena;
        class BlockPool;
        class BlockPoolManager;
        class GpuMemoryManager;

        // 内存数据库与表格
        // struct EntityKey; (migrated to FCT::ECS::entity)
        class DataBase;
        class ComponentDataBase;
        struct TextureRecord;
        class TextureTable;
        struct MeshRecord;
        class MeshTable;
        struct MaterialRecord;
        class MaterialTable;
        struct PipelineRecord;
        class PipelineTable;
        struct ShaderRecord;
        class ShaderTable;
        enum class GpuResourceType : uint8_t;
        enum class GpuResourceState : uint8_t;
        struct GpuResourceRecord;
        class GpuResourceTable;
        struct FieldDescriptor;
        struct TypeDescriptor;
        class TypeMetaTable;
        class SchemaTable;
        class TableRegistry;

        // 纹理格式解码与扩展注册
        struct DecodedTextureData;
        class ITextureFormatDecoder;
        class FctRhiTextureDecoder;
        class KtxContainerTextureDecoder;
        class TextureDecoderRegistry;

        // 门面模块
        class Resource;
    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_CLASSDEF_H
