#ifndef MQENGINE_RESOURCE_MEMORY_COMPONENTDATABASE_H
#define MQENGINE_RESOURCE_MEMORY_COMPONENTDATABASE_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/table/DataBase.h>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief ComponentDataBase：对齐规范后的兼容类，直接继承自统一内存数据库 DataBase
         */
        class ENGINE_API ComponentDataBase : public DataBase
        {
        public:
            using DataBase::DataBase;
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_MEMORY_COMPONENTDATABASE_H
