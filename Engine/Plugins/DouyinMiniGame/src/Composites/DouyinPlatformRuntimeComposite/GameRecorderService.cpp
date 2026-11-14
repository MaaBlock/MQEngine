/**
 * @file GameRecorderService.cpp
 * @brief FEAT-06 抖音 300s 环形录屏管理、高光切片与拍抖音视频话题挂载实现
 */
#include "GameRecorderService.h"

#include <algorithm>
#include <cmath>

namespace MQEngine::DouyinMiniGame
{
    GameRecorderService::GameRecorderService(IDouyinHostApi* hostApi)
        : m_hostApi(hostApi)
    {
    }

    Status GameRecorderService::start(int32_t requestedDurationSec)
    {
        if (requestedDurationSec <= 0)
        {
            return InvalidArgumentError("录屏时长必须大于 0 秒");
        }
        if (m_state == RecorderState::Recording)
        {
            return FailedPreconditionError("录屏已处于进行中状态");
        }

        // 抖音宿主环形录屏缓冲区最大支持 300 秒，超限自动截断至 300 秒
        m_activeDurationSec = std::min(requestedDurationSec, kMaxDurationSeconds);
        if (m_hostApi)
        {
            CHECK_STATUS(m_hostApi->recorderStart(m_activeDurationSec));
        }

        m_state = RecorderState::Recording;
        m_clipCount = 0;
        return OkStatus();
    }

    Status GameRecorderService::pause()
    {
        if (m_state != RecorderState::Recording)
        {
            return FailedPreconditionError("仅在 Recording 状态下可调用 pause()");
        }
        if (m_hostApi)
        {
            CHECK_STATUS(m_hostApi->recorderPause());
        }
        m_state = RecorderState::Paused;
        return OkStatus();
    }

    Status GameRecorderService::resume()
    {
        if (m_state != RecorderState::Paused)
        {
            return FailedPreconditionError("仅在 Paused 状态下可调用 resume()");
        }
        if (m_hostApi)
        {
            CHECK_STATUS(m_hostApi->recorderResume());
        }
        m_state = RecorderState::Recording;
        return OkStatus();
    }

    StatusOr<std::string> GameRecorderService::stop(double recordedElapsedSec)
    {
        if (m_state == RecorderState::Idle)
        {
            return FailedPreconditionError("当前未启动录屏，无法调用 stop()");
        }
        m_state = RecorderState::Idle;

        if (!std::isfinite(recordedElapsedSec) || recordedElapsedSec < kMinValidRecordDurationSec)
        {
            return FailedPreconditionError("录屏时长不足 3 秒下限，抖音宿主无法生成可分享视频文件");
        }

        if (m_hostApi)
        {
            auto pathOr = m_hostApi->recorderStop();
            if (!pathOr.ok())
            {
                return pathOr.status();
            }
            m_lastVideoPath = *pathOr;
        }
        else
        {
            m_lastVideoPath = "ttfile://temp/mq_highlight_clip.mp4";
        }
        return m_lastVideoPath;
    }

    Status GameRecorderService::recordHighlightClip(double beforeSec, double afterSec)
    {
        if (m_state != RecorderState::Recording)
        {
            return FailedPreconditionError("仅在录屏进行中可打点截取高光片段");
        }
        if (!std::isfinite(beforeSec) || !std::isfinite(afterSec) || beforeSec < 0.0 || afterSec < 0.0)
        {
            return InvalidArgumentError("高光截取前后时间窗口不能为负数");
        }
        if (beforeSec + afterSec <= 0.0 || beforeSec + afterSec > static_cast<double>(kMaxDurationSeconds))
        {
            return InvalidArgumentError("高光截取总窗口需位于 (0, 300] 秒区间内");
        }

        if (m_hostApi)
        {
            CHECK_STATUS(m_hostApi->recorderRecordClip(beforeSec, afterSec));
        }
        ++m_clipCount;
        return OkStatus();
    }

    void GameRecorderService::shareVideo(
        const DouyinShareVideoRequest& request,
        std::function<void(Status)> callback)
    {
        DouyinShareVideoRequest normalized = request;
        if (normalized.videoPath.empty())
        {
            normalized.videoPath = m_lastVideoPath;
        }
        if (normalized.videoPath.empty())
        {
            if (callback)
            {
                callback(InvalidArgumentError("分享视频路径 videoPath 为空，请先完成录屏"));
            }
            return;
        }

        if (!m_hostApi)
        {
            if (callback)
            {
                callback(OkStatus());
            }
            return;
        }

        m_hostApi->shareVideoMessage(normalized, std::move(callback));
    }
} // namespace MQEngine::DouyinMiniGame
