#include "../../engineapi.h"
#include "../../render/core/Uniform.h"
#define TEXT(str) (const char*)u8##str

namespace MQEngine
{
    const char* getEngineVersion()
    {
        return "0.0.1";
    }
    Runtime::Engine* getEngine()
    {
        return Runtime::Engine::getInstance();
    }

#ifdef FCT_DEBUG
    void OutputDebugObject()
    {
        _output_object(fout);
    }

    std::vector<FCT::_fct_object_t*> GetDebugObject()
    {
        return FCT::MemoryTracker::instance().snapshotObjects();
    }
#endif

}
