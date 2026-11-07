#ifndef MQENGINE_HOMELBM_THIRDPARTY_H
#define MQENGINE_HOMELBM_THIRDPARTY_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <FCT_Node.h>
#include <FCT.h>
#include <Engine/headers.h>
#include <FCT_Compute/FCT_Compute.h>
#include <ImguiPlugin/ImguiPlugin.h>

#ifdef OPAQUE
#undef OPAQUE
#endif
#ifdef ERROR
#undef ERROR
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
    #include <d3d12.h>
    #include <dxgi1_6.h>
    #ifdef BUILD_HOMELBM_PLUGIN
        #define HOME_LBM_API __declspec(dllexport)
    #else
        #define HOME_LBM_API __declspec(dllimport)
    #endif
#else
    #define HOME_LBM_API
#endif

namespace MQEngine {

using absl::FailedPreconditionError;
using absl::UnavailableError;

using FCT::makeUnique;
using FCT::UniquePtr;

} // namespace MQEngine

#endif // MQENGINE_HOMELBM_THIRDPARTY_H
