/**
 * @file thirdparty.h
 * @brief 引擎统一第三方库与 FCT 导出聚合头文件
 */
#ifndef ENGINE_THIRDPARTY_H
#define ENGINE_THIRDPARTY_H
#include <FCT_Base.h>
#include <FCT_Profiling.h>
#include <FCT_SAI.h>
#include <FCT_Node.h>
#include <FCT.h>
#include <FCT_NBR.h>
#ifndef __ANDROID__
#include <FCT_PyBri.h>
#endif
#if defined(__has_include)
#if __has_include(<FCT_IMGUI.h>)
#include <FCT_IMGUI.h>
#endif
#if __has_include(<imnodes.h>)
#include <imnodes.h>
#endif
#else
#include <FCT_IMGUI.h>
#include <imnodes.h>
#endif

#if defined(__wasm_simd128__)
#define MQENGINE_SIMD_WASM 1
#include <wasm_simd128.h>
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#define MQENGINE_SIMD_NEON 1
#include <arm_neon.h>
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define MQENGINE_SIMD_SSE 1
#include <immintrin.h>
#include <xmmintrin.h>
#include <emmintrin.h>
#else
#define MQENGINE_SIMD_SCALAR 1
#endif

#include <charconv>
#include <cmath>
#include <cstring>
#include <span>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/pfr.hpp>
#include <string>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/tag.hpp>
#include <boost/thread/future.hpp>
namespace boost {
#if defined(BOOST_THREAD_PROVIDES_FUTURE)
    template <typename T>
    using unique_future = future<T>;
#endif
}
#include <boost/json.hpp>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/flexbuffers.h>
#include <variant>
#include <spdlog/spdlog.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/numbers.h>
#include <absl/flags/flag.h>
#include <absl/flags/parse.h>
#include <absl/flags/reflection.h>
#include <MQEngine/thirdparty/FCTImporter.h>
#include <MQEngine/thirdparty/SpdlogImporter.h>
#include <MQEngine/base/CoreEngineManager.h>
#include <MQEngine/base/StaticTokenGraph.h>
namespace MQEngine
{
    template<typename T>
    using StatusOr = absl::StatusOr<T>;
    using Status = absl::Status;
    template<typename T>
    struct is_status_or : std::false_type {};
    template<typename T>
    struct is_status_or<absl::StatusOr<T>> : std::true_type {};

    template <typename T>
    Status GetStatusFrom(T&& data) {
        if constexpr (is_status_or<std::decay_t<T>>::value) {
            return std::forward<T>(data).status();
        } else {
            return std::forward<T>(data);
        }
    }

    /*
     * @brief 检查Status是否错误，如果错误就向上传递错误
     */
#define CHECK_STATUS(data) \
    do { \
        auto&& _mq_status_eval = (data); \
        if (!_mq_status_eval.ok()) { \
            return ::MQEngine::GetStatusFrom(std::forward<decltype(_mq_status_eval)>(_mq_status_eval)); \
        } \
    } while (0)

    using absl::InvalidArgumentError;
    using absl::AlreadyExistsError;
    using absl::FailedPreconditionError;
    using absl::UnimplementedError;
    using absl::OutOfRangeError;
    using absl::NotFoundError;
    using absl::CancelledError;
    using absl::InternalError;
    using absl::UnknownError;
    using absl::UnavailableError;
    using absl::ResourceExhaustedError;
    using absl::PermissionDeniedError;
    using absl::DataLossError;
    using absl::OkStatus;
    using absl::StrCat;
    using absl::IsNotFound;
    using absl::GetFlag;
    using absl::SetFlag;
    using absl::FindCommandLineFlag;
}
#include <MQEngine/base/CoreEngineModule.h>
#endif // ENGINE_THIRDPARTY_H
