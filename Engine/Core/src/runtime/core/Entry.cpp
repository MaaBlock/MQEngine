/**
 *@file entry.cpp
 *@brief The definition of entry point
 *@note The entry point has been unified in FCT library.
*/
#include "../../engineapi.h"
#include "Runtime.h"

using namespace MQEngine;

int ENGINE_API main(int argc, char* argv[])
{
    SetThreadName("Main / Logic Thread");
    SetupUtf8();
    MQEngine::Runtime::Runtime rt;
    rt.init(argc, argv);
    rt.run();
    rt.term();

    return 0;
}
