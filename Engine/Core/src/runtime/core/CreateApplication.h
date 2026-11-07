#ifndef CREATE_APPLICATION_H
#define CREATE_APPLICATION_H

#include "../interface/Application.h"

namespace MQEngine
{
    using CreateApplicationFn = Runtime::Application* (*)();

    extern CreateApplicationFn CreateApplication;
}

#endif // CREATE_APPLICATION_H
