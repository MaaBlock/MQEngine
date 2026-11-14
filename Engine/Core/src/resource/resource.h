#ifndef MQENGINE_RESOURCE_H
#define MQENGINE_RESOURCE_H

#include <MQEngine/resource/classdef.h>
#include <MQEngine/resource/config/Config.h>
#include <MQEngine/resource/config/CommandLineParser.h>
#include <MQEngine/resource/core/MemoryTracker.h>
#include <MQEngine/resource/core/Arena.h>
#include <MQEngine/resource/core/BlockPool.h>
#include <MQEngine/resource/core/GpuMemoryManager.h>
#include <MQEngine/resource/table/DataBase.h>
#include <MQEngine/resource/memory/ComponentDataBase.h>
#include <MQEngine/resource/table/GpuResourceTable.h>
#include <MQEngine/resource/table/TextureTable.h>
#include <MQEngine/resource/table/MeshTable.h>
#include <MQEngine/resource/table/MaterialTable.h>
#include <MQEngine/resource/table/PipelineTable.h>
#include <MQEngine/resource/table/ShaderTable.h>
#include <MQEngine/resource/table/TypeMetaTable.h>
#include <MQEngine/resource/table/SchemaTable.h>
#include <MQEngine/resource/table/TableRegistry.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/resource/core/DiskResourceLoader.h>
#include <MQEngine/resource/core/DataLoader.h>
#include <MQEngine/resource/core/FileDataLoader.h>
#include <MQEngine/resource/core/TextureDecoderRegistry.h>
#include <MQEngine/resource/core/resource.h>

#endif // MQENGINE_RESOURCE_H
