/**
 * @file webjs_thirdparty.h
 * @brief 抖音运行时宿主桥接专用聚合头（仅接入 FCT_Base 与 FCT_WebJS，与桌面 FCT_Node 物理隔离）
 */
#ifndef MQENGINE_DOUYINMINIGAME_WEBJS_THIRDPARTY_H
#define MQENGINE_DOUYINMINIGAME_WEBJS_THIRDPARTY_H

#include <FCT_Base.h>
#include <FCT_WebJS/WebJS.h>

namespace MQEngine
{
    using FCT::Status;
    using FCT::StatusOr;
    using FCT::UniquePtr;
    using FCT::makeUnique;
    using FCT::Ptr;
    using absl::OkStatus;
    using absl::InvalidArgumentError;
    using absl::NotFoundError;
    using absl::FailedPreconditionError;
    using absl::PermissionDeniedError;
    using absl::ResourceExhaustedError;
    using absl::InternalError;
} // namespace MQEngine

#endif // MQENGINE_DOUYINMINIGAME_WEBJS_THIRDPARTY_H
