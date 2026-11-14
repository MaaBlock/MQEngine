#ifndef CREATE_APPLICATION_H
#define CREATE_APPLICATION_H

#include <MQEngine/runtime/interface/Application.h>

namespace MQEngine
{
    using CreateApplicationFn = Runtime::Application* (*)();

    extern ENGINE_API CreateApplicationFn CreateApplication;
    ENGINE_API FCT::MemoryTracker* GetSharedMemoryTracker() noexcept;

    inline void BindModuleSharedMemoryTracker() noexcept
    {
        FCT::MemoryTracker::setSharedInstance(GetSharedMemoryTracker());
    }
}

#endif // CREATE_APPLICATION_H
