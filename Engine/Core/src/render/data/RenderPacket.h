#ifndef ENGINE_RENDERER_RENDER_PACKET_H
#define ENGINE_RENDERER_RENDER_PACKET_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "../../base/Computation.h"
#include "../../thirdparty/thirdparty.h"

namespace MQEngine {

    /**
     * @brief 渲染数据包类型枚举
     */
    enum class RenderPacketType : uint8_t {
        General = 0,        ///< 通用渲染帧数据
        MeshUpdate,         ///< 几何体/网格更新
        MaterialUpdate,     ///< 材质/着色器参数更新
        CameraUpdate,       ///< 视口与摄像机变换更新
        CustomCommand       ///< 自定义渲染指令
    };

    /**
     * @brief 渲染清除控制掩码
     */
    enum class RenderClearFlags : uint32_t {
        None    = 0,
        Color   = 1 << 0,
        Depth   = 1 << 1,
        Stencil = 1 << 2,
        All     = Color | Depth | Stencil
    };

    /**
     * @brief 几何绘制图元指令
     */
    struct RenderDrawItem {
        uint64_t entityId{0};                       ///< 实体 ID
        uint64_t meshId{0};                         ///< 网格资源 ID
        uint64_t materialId{0};                     ///< 材质/着色器管线 ID
        std::string meshName;                       ///< 网格资产名称或 UUID
        std::string materialName;                   ///< 材质管线标识
        FCT::Mat4 modelMatrix;                      ///< 模型世界变换矩阵
        FCT::Mat4 worldTransform;                   ///< 世界空间变换别名
        uint32_t indexCount{0};                     ///< 索引数量 (0 表示纯顶点绘制)
        uint32_t firstIndex{0};                     ///< 索引起始偏移
        int32_t baseVertex{0};                      ///< 基础顶点偏移
        uint32_t passMask{0xFFFFFFFF};              ///< 绘制通道掩码
        uint32_t customFlags{0};                    ///< 自定义控制标志

        RenderDrawItem() = default;
        ~RenderDrawItem() = default;

        RenderDrawItem(const RenderDrawItem&) = default;
        RenderDrawItem& operator=(const RenderDrawItem&) = default;
        RenderDrawItem(RenderDrawItem&&) noexcept = default;
        RenderDrawItem& operator=(RenderDrawItem&&) noexcept = default;
    };

    /**
     * @brief 跨线程渲染帧数据包 (RenderPacket)
     * 
     * 承载逻辑线程或工作线程池计算完成的单帧渲染快照。
     * 具备值语义与移动语义 (Move-Only)，可零堆内存分配内联存储于 Computation<RenderPacket> 内部。
     */
    struct RenderPacket {
        // ==================== 帧时序与状态元数据 ====================
        uint64_t frameIndex{0};                     ///< 单调递增逻辑帧序号
        float deltaTime{0.0f};                      ///< 逻辑帧时间步长
        uint64_t timestampNs{0};                    ///< 纳秒级高精度时间戳
        RenderPacketType packetType{RenderPacketType::General}; ///< 数据包类型

        // ==================== 视口与帧缓冲清除参数 ====================
        uint32_t viewportWidth{0};                  ///< 视口宽度
        uint32_t viewportHeight{0};                 ///< 视口高度
        FCT::Vec4 clearColor{0.1f, 0.1f, 0.15f, 1.0f}; ///< 视口清除背景色 (RGBA)
        float clearDepth{1.0f};                     ///< 深度清除值 (标准Z: 1.0)
        uint32_t clearStencil{0};                   ///< 模板清除值
        uint32_t clearFlags{static_cast<uint32_t>(RenderClearFlags::All)}; ///< 清除掩码

        // ==================== 相机变换与空间参数 ====================
        FCT::Mat4 viewMatrix;                       ///< 视图变换矩阵
        FCT::Mat4 projMatrix;                       ///< 投影变换矩阵
        FCT::Mat4 viewProjMatrix;                   ///< 视图投影联合矩阵
        FCT::Vec3<float> cameraPosition{0.0f, 0.0f, 0.0f}; ///< 摄像机世界坐标

        // ==================== 绘制指令列表与动态载荷 ====================
        std::vector<RenderDrawItem> drawItems;       ///< 绘制图元指令列表
        std::vector<uint8_t> payloadData;           ///< 动态 Uniform/常量缓冲载荷
        std::string debugTag;                       ///< 调试标识名

        // ==================== 构造与生命周期 ====================
        RenderPacket() = default;
        ~RenderPacket() = default;

        // 显式禁用隐式拷贝 (Move-Only 语义，彻底杜绝隐性大内存复制)
        RenderPacket(const RenderPacket&) = delete;
        RenderPacket& operator=(const RenderPacket&) = delete;

        // 移动构造函数 (noexcept)
        RenderPacket(RenderPacket&& other) noexcept = default;

        // 移动赋值操作符 (noexcept)
        RenderPacket& operator=(RenderPacket&& other) noexcept = default;

        /**
         * @brief 重置为确定性初始空状态
         */
        void reset() noexcept {
            frameIndex = 0;
            deltaTime = 0.0f;
            timestampNs = 0;
            packetType = RenderPacketType::General;
            viewportWidth = 0;
            viewportHeight = 0;
            clearColor = FCT::Vec4{0.1f, 0.1f, 0.15f, 1.0f};
            clearDepth = 1.0f;
            clearStencil = 0;
            clearFlags = static_cast<uint32_t>(RenderClearFlags::All);
            viewMatrix.identity();
            projMatrix.identity();
            viewProjMatrix.identity();
            cameraPosition = FCT::Vec3<float>{0.0f, 0.0f, 0.0f};
            drawItems.clear();
            payloadData.clear();
            debugTag.clear();
        }

        /**
         * @brief 显式深拷贝副本 (仅供调试与快照需求使用)
         */
        RenderPacket clone() const {
            RenderPacket copy;
            copy.frameIndex = frameIndex;
            copy.deltaTime = deltaTime;
            copy.timestampNs = timestampNs;
            copy.packetType = packetType;
            copy.viewportWidth = viewportWidth;
            copy.viewportHeight = viewportHeight;
            copy.clearColor = clearColor;
            copy.clearDepth = clearDepth;
            copy.clearStencil = clearStencil;
            copy.clearFlags = clearFlags;
            copy.viewMatrix = viewMatrix;
            copy.projMatrix = projMatrix;
            copy.viewProjMatrix = viewProjMatrix;
            copy.cameraPosition = cameraPosition;
            copy.drawItems = drawItems;
            copy.payloadData = payloadData;
            copy.debugTag = debugTag;
            return copy;
        }

        /**
         * @brief 检查数据包是否具备有效渲染属性
         */
        bool isValid() const noexcept {
            return (viewportWidth > 0 && viewportHeight > 0) || !drawItems.empty();
        }

        /**
         * @brief 设置视口尺寸便捷辅助方法
         */
        void setViewport(uint32_t width, uint32_t height) noexcept {
            viewportWidth = width;
            viewportHeight = height;
        }

        /**
         * @brief 设置相机矩阵与坐标便捷辅助方法
         */
        void setCamera(const FCT::Mat4& view, const FCT::Mat4& proj, const FCT::Vec3<float>& pos) noexcept {
            viewMatrix = view;
            projMatrix = proj;
            viewProjMatrix = proj * view;
            cameraPosition = pos;
        }

        /**
         * @brief 添加图元指令
         */
        void addDrawItem(RenderDrawItem&& item) {
            drawItems.push_back(std::move(item));
        }
    };

    /**
     * @brief 活跃渲染帧状态 (RenderFrameState)
     * 
     * 渲染线程消费并持久保留的渲染快照，支持无就绪新包时的视觉连续性复用
     */
    struct RenderFrameState {
        uint64_t lastUpdatedFrameIndex{0};          ///< 最近一次更新时的逻辑帧号
        uint64_t frameIndex{0};                     ///< 活跃帧序号
        float deltaTime{0.0f};                      ///< 帧时间步长
        uint64_t timestampNs{0};                    ///< 纳秒时间戳
        RenderPacketType packetType{RenderPacketType::General}; ///< 数据包类型
        uint32_t viewportWidth{0};                  ///< 视口宽度
        uint32_t viewportHeight{0};                 ///< 视口高度
        FCT::Vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f}; ///< 清屏背景色
        float clearDepth{1.0f};                     ///< 深度清除值
        uint32_t clearStencil{0};                   ///< 模板清除值
        uint32_t clearFlags{0};                     ///< 清除掩码
        FCT::Mat4 viewMatrix;                       ///< 视图变换矩阵
        FCT::Mat4 projMatrix;                       ///< 投影变换矩阵
        FCT::Mat4 viewProjMatrix;                   ///< 联合投影矩阵
        FCT::Vec3<float> cameraPosition{0.0f, 0.0f, 0.0f}; ///< 相机世界坐标
        std::vector<RenderDrawItem> activeDrawItems; ///< 活跃图元列表
        std::vector<uint8_t> payloadData;           ///< 载荷数据
        std::string debugTag;                       ///< 调试标签
        bool isValid{false};                        ///< 是否处于有效就绪状态

        /**
         * @brief 从就绪渲染包全量移动更新当前帧状态
         */
        void updateFromPacket(RenderPacket&& packet) {
            lastUpdatedFrameIndex = packet.frameIndex;
            frameIndex = packet.frameIndex;
            deltaTime = packet.deltaTime;
            timestampNs = packet.timestampNs;
            packetType = packet.packetType;
            viewportWidth = packet.viewportWidth;
            viewportHeight = packet.viewportHeight;
            clearColor = packet.clearColor;
            clearDepth = packet.clearDepth;
            clearStencil = packet.clearStencil;
            clearFlags = packet.clearFlags;
            viewMatrix = packet.viewMatrix;
            projMatrix = packet.projMatrix;
            viewProjMatrix = packet.viewProjMatrix;
            cameraPosition = packet.cameraPosition;
            activeDrawItems = std::move(packet.drawItems);
            payloadData = std::move(packet.payloadData);
            debugTag = std::move(packet.debugTag);
            isValid = true;
        }

        /**
         * @brief 重置为无效初始状态
         */
        void reset() {
            lastUpdatedFrameIndex = 0;
            frameIndex = 0;
            deltaTime = 0.0f;
            timestampNs = 0;
            packetType = RenderPacketType::General;
            viewportWidth = 0;
            viewportHeight = 0;
            clearColor = FCT::Vec4{0.0f, 0.0f, 0.0f, 1.0f};
            clearDepth = 1.0f;
            clearStencil = 0;
            clearFlags = 0;
            viewMatrix.identity();
            projMatrix.identity();
            viewProjMatrix.identity();
            cameraPosition = FCT::Vec3<float>{0.0f, 0.0f, 0.0f};
            activeDrawItems.clear();
            payloadData.clear();
            debugTag.clear();
            isValid = false;
        }
    };

    // 编译期静态断言保障
    static_assert(!std::is_copy_constructible_v<RenderPacket>, "RenderPacket 必须严禁隐式拷贝");
    static_assert(!std::is_copy_assignable_v<RenderPacket>, "RenderPacket 必须严禁隐式拷贝赋值");
    static_assert(std::is_nothrow_move_constructible_v<RenderPacket>, "RenderPacket 移动构造必须 noexcept");
    static_assert(std::is_nothrow_move_assignable_v<RenderPacket>, "RenderPacket 移动赋值必须 noexcept");

} // namespace MQEngine

#endif // ENGINE_RENDERER_RENDER_PACKET_H
