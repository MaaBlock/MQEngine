#ifndef CREATE_APPLICATION_H
#define CREATE_APPLICATION_H

#include "application.h"

namespace MQEngine
{
    using CreateApplicationFn = Application* (*)();

    extern CreateApplicationFn GCreateApplication;
}

#endif // CREATE_APPLICATION_H
