/**
 * @file ThirdParty.h
 * @brief A header file containing third party libraries and macros for platform
 *
 */
#ifndef ENGINE_THIRDPARTY_H
#define ENGINE_THIRDPARTY_H
#include <FCT_Base.h>
#include <FCT_SAI.h>
#include <FCT_Node.h>
#include <FCT.h>
#include <FCT_NBR.h>
#ifndef __ANDROID__
#include <FCT_PyBri.h>
#endif
#include <FCT_IMGUI.h>

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
#include <absl/flags/flag.h>
#include <absl/flags/parse.h>
#include <absl/flags/reflection.h>
#include "FCTImporter.h"
#include "SpdlogImporter.h"
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
    using absl::FailedPreconditionError;
    using absl::UnimplementedError;
    using absl::OutOfRangeError;
    using absl::NotFoundError;
    using absl::InternalError;
    using absl::UnknownError;
    using absl::OkStatus;
    using absl::StrCat;
    using absl::IsNotFound;
    using absl::GetFlag;
    using absl::SetFlag;
    using absl::FindCommandLineFlag;
    //using EventPipe = FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly>;
}
#endif //THIRDPARTY_H
