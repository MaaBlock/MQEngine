#include "../engineapi.h"
#include "./Uniform.h"
#define TEXT(str) (const char*)u8##str

namespace MQEngine
{
    Engine* Engine::s_instance = nullptr;
    //Engine* Engine::s_instance = nullptr;
    const char* getEngineVersion()
    {
        return "0.0.1";
    }
    Engine& getEngine() {
        return Engine::getInstance();
    }

#ifdef FCT_DEBUG
    void OutputDebugObject()
    {
        _output_object(fout);
    }

    std::vector<FCT::_fct_object_t*> GetDebugObject()
    {
        return fct_object_list;
    }
#endif

}
