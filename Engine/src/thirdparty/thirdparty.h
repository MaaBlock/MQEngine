/**
 * @file ThirdParty.h
 * @brief A header file containing third party libraries and macros for platform
 *
 */
#ifndef ENGINE_THIRDPARTY_H
#define ENGINE_THIRDPARTY_H
#include <FCT_Node.h>
#include <FCT.h>
#include <FCT_IMGUI.h>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <string>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/thread/future.hpp>
#include <spdlog/spdlog.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>
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
    auto GetStatusFrom(T&& data) -> decltype(auto) {
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
        if (!(data).ok()) { \
            return GetStatusFrom(data); \
        } \
    } while (0)

    using absl::InvalidArgumentError;
    using absl::UnimplementedError;
    using absl::OutOfRangeError;
    using absl::NotFoundError;
    using absl::InternalError;
    using absl::UnknownError;
    using absl::OkStatus;
    using absl::StrCat;
    using FCT::Context;
    using FCT::Window;
    using FCT::Uniform;
    using FCT::ShaderRef;
    using FCT::UniquePtr;
    using FCT::ModelLoader;
    using FCT::Sampler;
    using FCT::Runtime;
    //using EventPipe = FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly>;
}
#endif //THIRDPARTY_H
