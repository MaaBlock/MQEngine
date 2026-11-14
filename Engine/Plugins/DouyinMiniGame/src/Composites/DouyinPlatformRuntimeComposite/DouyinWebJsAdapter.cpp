/**
 * @file DouyinWebJsAdapter.cpp
 * @brief 基于 FCT_WebJS 句柄表与微任务队列的抖音 tt.* 宿主 API 桥接实现
 */
#define MQ_DOUYIN_WEBJS_ONLY 1
#include "DouyinWebJsAdapter.h"

namespace MQEngine::DouyinMiniGame
{
    namespace
    {
        class WebJsDouyinHostApi : public IDouyinHostApi
        {
        public:
            WebJsDouyinHostApi() = default;
            ~WebJsDouyinHostApi() override
            {
                m_onShowListener = nullptr;
                m_onMemoryWarningListener = nullptr;
                if (isLiveHandle(m_onShowCbHolder.getHandle()))
                {
                    m_onShowCbHolder.registerMethod("invoke", [](const std::vector<FCT::JS::Value>&) {
                        return FCT::JS::Value();
                    });
                }
                if (isLiveHandle(m_onMemoryWarningCbHolder.getHandle()))
                {
                    m_onMemoryWarningCbHolder.registerMethod("invoke", [](const std::vector<FCT::JS::Value>&) {
                        return FCT::JS::Value();
                    });
                }
            }

            void bindOnShowListener(std::function<void(const DouyinShowEvent&)> listener) override
            {
                m_onShowListener = std::move(listener);
                FCT::JS::Object ttObj = getOrEnsureTtObject();
                if (!ttObj.has("onShow"))
                {
                    return;
                }

                m_onShowCbHolder = env().newObject();
                m_onShowCbHolder.registerMethod("invoke", [this](const std::vector<FCT::JS::Value>& args) -> FCT::JS::Value {
                    if (m_onShowListener && !args.empty() && isLiveObject(args[0]))
                    {
                        FCT::JS::Object res = args[0].asObject();
                        DouyinShowEvent evt;
                        evt.scene = res.get("scene").isString() ? res.get("scene").toString() : "";
                        evt.launchFrom = res.get("launch_from").isString() ? res.get("launch_from").toString() : "";
                        evt.location = res.get("location").isString() ? res.get("location").toString() : "";
                        m_onShowListener(evt);
                    }
                    return FCT::JS::Value();
                });
                (void)ttObj.call("onShow", {FCT::JS::Value(FCT::JS::ValueType::Object, m_onShowCbHolder.getHandle())});
            }

            void bindOnMemoryWarningListener(std::function<void(int32_t level)> listener) override
            {
                m_onMemoryWarningListener = std::move(listener);
                FCT::JS::Object ttObj = getOrEnsureTtObject();
                if (!ttObj.has("onMemoryWarning"))
                {
                    return;
                }

                m_onMemoryWarningCbHolder = env().newObject();
                m_onMemoryWarningCbHolder.registerMethod("invoke", [this](const std::vector<FCT::JS::Value>& args) -> FCT::JS::Value {
                    if (m_onMemoryWarningListener && !args.empty() && isLiveObject(args[0]))
                    {
                        FCT::JS::Object res = args[0].asObject();
                        const int32_t lvl = res.get("level").isNumber() ? res.get("level").toInt32() : 10;
                        m_onMemoryWarningListener(lvl);
                    }
                    return FCT::JS::Value();
                });
                (void)ttObj.call("onMemoryWarning", {FCT::JS::Value(FCT::JS::ValueType::Object, m_onMemoryWarningCbHolder.getHandle())});
            }

            void checkSidebarScene(std::function<void(StatusOr<bool>)> callback) override
            {
                FCT::JS::Object ttObj = getOrEnsureTtObject();
                if (ttObj.has("checkScene"))
                {
                    FCT::JS::Object opts = env().newObject();
                    opts.set("scene", FCT::JS::Value("sidebar"));
                    FCT::JS::Value ret = ttObj.call("checkScene", {FCT::JS::Value(FCT::JS::ValueType::Object, opts.getHandle())});
                    if (ret.isBoolean())
                    {
                        if (callback) callback(ret.toBoolean());
                        return;
                    }
                    if (isLiveObject(ret))
                    {
                        FCT::JS::Object ro = ret.asObject();
                        if (ro.has("isExist"))
                        {
                            if (callback) callback(ro.get("isExist").toBoolean());
                            return;
                        }
                    }
                }
                if (callback)
                {
                    callback(true);
                }
            }

            void navigateToSidebarScene(std::function<void(Status)> callback) override
            {
                FCT::JS::Object ttObj = getOrEnsureTtObject();
                if (ttObj.has("navigateToScene"))
                {
                    FCT::JS::Object opts = env().newObject();
                    opts.set("scene", FCT::JS::Value("sidebar"));
                    FCT::JS::Value ret = ttObj.call("navigateToScene", {FCT::JS::Value(FCT::JS::ValueType::Object, opts.getHandle())});
                    if (ret.isBoolean() && !ret.toBoolean())
                    {
                        if (callback) callback(InternalError("tt.navigateToScene 跳转失败"));
                        return;
                    }
                }
                if (callback)
                {
                    callback(OkStatus());
                }
            }

            Status createRewardedVideoAd(const std::string& adUnitId) override
            {
                if (adUnitId.empty())
                {
                    return InvalidArgumentError("adUnitId 不能为空");
                }
                FCT::JS::Object ttObj = getOrEnsureTtObject();
                if (ttObj.has("createRewardedVideoAd"))
                {
                    FCT::JS::Object opts = env().newObject();
                    opts.set("adUnitId", FCT::JS::Value(adUnitId));
                    FCT::JS::Value adVal = ttObj.call("createRewardedVideoAd", {FCT::JS::Value(FCT::JS::ValueType::Object, opts.getHandle())});
                    if (isLiveObject(adVal))
                    {
                        m_rewardedAdObj = adVal.asObject();
                    }
                }
                m_currentAdUnitId = adUnitId;
                return OkStatus();
            }

            Status loadRewardedVideoAd() override
            {
                if (m_currentAdUnitId.empty())
                {
                    return FailedPreconditionError("激励视频广告实例尚未初始化");
                }
                if (isLiveHandle(m_rewardedAdObj.getHandle()) && m_rewardedAdObj.has("load"))
                {
                    (void)m_rewardedAdObj.call("load");
                }
                return OkStatus();
            }

            void showRewardedVideoAd(
                std::function<void(bool isEnded)> onClose,
                std::function<void(int32_t errCode, const std::string& errMsg)> onError) override
            {
                if (isLiveHandle(m_rewardedAdObj.getHandle()) && m_rewardedAdObj.has("show"))
                {
                    FCT::JS::Value ret = m_rewardedAdObj.call("show");
                    if (isLiveObject(ret))
                    {
                        FCT::JS::Object res = ret.asObject();
                        if (res.has("errCode") && res.get("errCode").toInt32() != 0)
                        {
                            if (onError)
                            {
                                onError(res.get("errCode").toInt32(), res.get("errMsg").toString());
                            }
                            return;
                        }
                        if (res.has("isEnded"))
                        {
                            if (onClose)
                            {
                                onClose(res.get("isEnded").toBoolean());
                            }
                            return;
                        }
                    }
                }
                // Note: 脱离浏览器/真机宿主时保持挂起态，交由上层服务显式驱动模拟收盘事件（严禁静默假发奖励）
                (void)onClose;
                (void)onError;
            }

            Status recorderStart(int32_t durationSec) override
            {
                FCT::JS::Object rec = getRecorderObject();
                if (rec.isValid() && rec.has("start"))
                {
                    FCT::JS::Object opts = env().newObject();
                    opts.set("duration", FCT::JS::Value(durationSec));
                    (void)rec.call("start", {FCT::JS::Value(FCT::JS::ValueType::Object, opts.getHandle())});
                }
                return OkStatus();
            }

            Status recorderPause() override
            {
                FCT::JS::Object rec = getRecorderObject();
                if (rec.isValid() && rec.has("pause"))
                {
                    (void)rec.call("pause");
                }
                return OkStatus();
            }

            Status recorderResume() override
            {
                FCT::JS::Object rec = getRecorderObject();
                if (rec.isValid() && rec.has("resume"))
                {
                    (void)rec.call("resume");
                }
                return OkStatus();
            }

            StatusOr<std::string> recorderStop() override
            {
                FCT::JS::Object rec = getRecorderObject();
                if (rec.isValid() && rec.has("stop"))
                {
                    FCT::JS::Value ret = rec.call("stop");
                    if (ret.isString() && !ret.toString().empty())
                    {
                        return ret.toString();
                    }
                    if (isLiveObject(ret))
                    {
                        FCT::JS::Object retObj = ret.asObject();
                        if (retObj.has("videoPath"))
                        {
                            return retObj.get("videoPath").toString();
                        }
                    }
                }
                return std::string("ttfile://temp/mq_highlight_clip.mp4");
            }

            Status recorderRecordClip(double beforeSec, double afterSec) override
            {
                FCT::JS::Object rec = getRecorderObject();
                if (rec.isValid() && rec.has("recordClip"))
                {
                    FCT::JS::Array range = env().newArray(2);
                    range.set(0, FCT::JS::Value(beforeSec));
                    range.set(1, FCT::JS::Value(afterSec));
                    FCT::JS::Object opts = env().newObject();
                    opts.set("timeRange", FCT::JS::Value(FCT::JS::ValueType::Array, range.getHandle()));
                    (void)rec.call("recordClip", {FCT::JS::Value(FCT::JS::ValueType::Object, opts.getHandle())});
                }
                return OkStatus();
            }

            void shareVideoMessage(
                const DouyinShareVideoRequest& req,
                std::function<void(Status)> callback) override
            {
                FCT::JS::Object ttObj = getOrEnsureTtObject();
                if (ttObj.has("shareAppMessage"))
                {
                    FCT::JS::Object opts = env().newObject();
                    opts.set("channel", FCT::JS::Value("video"));
                    opts.set("title", FCT::JS::Value(req.title));

                    FCT::JS::Object extra = env().newObject();
                    extra.set("videoPath", FCT::JS::Value(req.videoPath));
                    FCT::JS::Array topicsArr = env().newArray(req.videoTopics.size());
                    for (size_t i = 0; i < req.videoTopics.size(); ++i)
                    {
                        topicsArr.set(i, FCT::JS::Value(req.videoTopics[i]));
                    }
                    extra.set("videoTopics", FCT::JS::Value(FCT::JS::ValueType::Array, topicsArr.getHandle()));
                    extra.set("hashtag_list", FCT::JS::Value(FCT::JS::ValueType::Array, topicsArr.getHandle()));
                    opts.set("extra", FCT::JS::Value(FCT::JS::ValueType::Object, extra.getHandle()));

                    FCT::JS::Value ret = ttObj.call("shareAppMessage", {FCT::JS::Value(FCT::JS::ValueType::Object, opts.getHandle())});
                    if (ret.isBoolean() && !ret.toBoolean())
                    {
                        if (callback) callback(InternalError("用户取消或宿主拒绝视频分享"));
                        return;
                    }
                }
                if (callback)
                {
                    callback(OkStatus());
                }
            }

            DouyinSystemSafeArea querySystemSafeArea() override
            {
                DouyinSystemSafeArea info;
                FCT::JS::Object ttObj = getOrEnsureTtObject();
                if (ttObj.has("getSystemInfoSync"))
                {
                    FCT::JS::Value v = ttObj.call("getSystemInfoSync");
                    if (isLiveObject(v))
                    {
                        FCT::JS::Object sys = v.asObject();
                        if (sys.get("windowWidth").isNumber()) info.windowWidth = static_cast<float>(sys.get("windowWidth").toNumber());
                        if (sys.get("windowHeight").isNumber()) info.windowHeight = static_cast<float>(sys.get("windowHeight").toNumber());
                        if (sys.get("pixelRatio").isNumber()) info.pixelRatio = static_cast<float>(sys.get("pixelRatio").toNumber());
                        else if (sys.get("devicePixelRatio").isNumber()) info.pixelRatio = static_cast<float>(sys.get("devicePixelRatio").toNumber());

                        const FCT::JS::Value saVal = sys.get("safeArea");
                        if (isLiveObject(saVal))
                        {
                            FCT::JS::Object sa = saVal.asObject();
                            if (sa.get("left").isNumber()) info.safeLeft = static_cast<float>(sa.get("left").toNumber());
                            if (sa.get("top").isNumber()) info.safeTop = static_cast<float>(sa.get("top").toNumber());
                            if (sa.get("right").isNumber()) info.safeRight = static_cast<float>(sa.get("right").toNumber());
                            if (sa.get("bottom").isNumber()) info.safeBottom = static_cast<float>(sa.get("bottom").toNumber());
                        }
                    }
                }
                return info;
            }

            DouyinCapsuleRect queryMenuButtonCapsuleRect() override
            {
                DouyinCapsuleRect rect{290.0f, 48.0f, 378.0f, 80.0f, 88.0f, 32.0f};
                FCT::JS::Object ttObj = getOrEnsureTtObject();
                if (ttObj.has("getMenuButtonBoundingClientRect"))
                {
                    FCT::JS::Value v = ttObj.call("getMenuButtonBoundingClientRect");
                    if (isLiveObject(v))
                    {
                        FCT::JS::Object r = v.asObject();
                        if (r.get("left").isNumber()) rect.left = static_cast<float>(r.get("left").toNumber());
                        if (r.get("top").isNumber()) rect.top = static_cast<float>(r.get("top").toNumber());
                        if (r.get("right").isNumber()) rect.right = static_cast<float>(r.get("right").toNumber());
                        if (r.get("bottom").isNumber()) rect.bottom = static_cast<float>(r.get("bottom").toNumber());
                        if (r.get("width").isNumber()) rect.width = static_cast<float>(r.get("width").toNumber());
                        if (r.get("height").isNumber()) rect.height = static_cast<float>(r.get("height").toNumber());
                    }
                }
                return rect;
            }

            Status postOpenDataMessage(const std::string& jsonPayload) override
            {
                FCT::JS::Object ttObj = getOrEnsureTtObject();
                if (ttObj.has("getOpenDataContext"))
                {
                    FCT::JS::Value odcVal = ttObj.call("getOpenDataContext");
                    if (isLiveObject(odcVal))
                    {
                        FCT::JS::Object odc = odcVal.asObject();
                        if (odc.has("postMessage"))
                        {
                            (void)odc.call("postMessage", {FCT::JS::Value(jsonPayload)});
                        }
                    }
                }
                return OkStatus();
            }

            bool readSharedRankCanvasRgba(
                uint32_t& outWidth,
                uint32_t& outHeight,
                std::vector<uint8_t>& outRgbaPixels) override
            {
                outWidth = 256u;
                outHeight = 256u;
                FCT::JS::Object ttObj = getOrEnsureTtObject();
                if (ttObj.has("getOpenDataContext"))
                {
                    FCT::JS::Value odcVal = ttObj.call("getOpenDataContext");
                    if (isLiveObject(odcVal))
                    {
                        FCT::JS::Object odc = odcVal.asObject();
                        const FCT::JS::Value canvasVal = odc.get("canvas");
                        if (isLiveObject(canvasVal))
                        {
                            FCT::JS::Object canvas = canvasVal.asObject();
                            if (canvas.get("width").isNumber() && canvas.get("width").toInt32() > 0)
                            {
                                outWidth = static_cast<uint32_t>(canvas.get("width").toInt32());
                            }
                            if (canvas.get("height").isNumber() && canvas.get("height").toInt32() > 0)
                            {
                                outHeight = static_cast<uint32_t>(canvas.get("height").toInt32());
                            }
                        }
                    }
                }
                const size_t totalBytes = static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4u;
                outRgbaPixels.assign(totalBytes, 0u);
                return true;
            }

            void dismissBootSplash() override
            {
                FCT::JS::Object glob = env().global();
                const FCT::JS::Value relayVal = glob.get("__MQ_SPLASH_RELAY__");
                if (isLiveObject(relayVal))
                {
                    FCT::JS::Object relay = relayVal.asObject();
                    if (relay.has("dismiss"))
                    {
                        (void)relay.call("dismiss");
                    }
                }
            }

            void pumpMicrotasks() override
            {
                env().runMicrotasks();
            }

        private:
            static FCT::JS::WebJSEnvironment& env()
            {
                return FCT::JS::WebJSEnvironment::instance();
            }

            static bool isLiveHandle(uint32_t handle)
            {
                return handle != FCT::JS::HandleUndefined &&
                       handle != FCT::JS::HandleNull &&
                       env().getVMObject(handle) != nullptr;
            }

            static bool isLiveObject(const FCT::JS::Value& val)
            {
                return val.isObject() && isLiveHandle(val.getHandle());
            }

            FCT::JS::Object getOrEnsureTtObject()
            {
                FCT::JS::Object glob = env().global();
                FCT::JS::Value existing = glob.get("tt");
                if (isLiveObject(existing))
                {
                    return existing.asObject();
                }
                FCT::JS::Object created = env().newObject();
                env().retainHandle(created.getHandle());
                glob.set("tt", FCT::JS::Value(FCT::JS::ValueType::Object, created.getHandle()));
                return created;
            }

            FCT::JS::Object getRecorderObject()
            {
                FCT::JS::Object ttObj = getOrEnsureTtObject();
                if (ttObj.has("getGameRecorderManager"))
                {
                    FCT::JS::Value v = ttObj.call("getGameRecorderManager");
                    if (isLiveObject(v))
                    {
                        return v.asObject();
                    }
                }
                return FCT::JS::Object();
            }

        private:
            std::function<void(const DouyinShowEvent&)> m_onShowListener;
            std::function<void(int32_t)> m_onMemoryWarningListener;
            FCT::JS::Object m_onShowCbHolder;
            FCT::JS::Object m_onMemoryWarningCbHolder;
            FCT::JS::Object m_rewardedAdObj;
            std::string m_currentAdUnitId;
        };
    } // namespace

    UniquePtr<IDouyinHostApi> createWebJsDouyinHostApi()
    {
        return makeUnique<WebJsDouyinHostApi>();
    }
} // namespace MQEngine::DouyinMiniGame
