#pragma once

#include <cstdint>
#include <string>

namespace MQEngine::Testing {

/**
 * @brief 离屏无头 GLES 3.0 / EGL Pbuffer 测试夹具（支持 ANGLE 运行时优雅跳过降级）
 */
class HeadlessGlesFixture {
public:
    HeadlessGlesFixture();
    virtual ~HeadlessGlesFixture();

    /**
     * @brief 尝试初始化无头离屏 EGL / GLES 上下文
     * @param width 离屏 Pbuffer 宽度（默认 64）
     * @param height 离屏 Pbuffer 高度（默认 64）
     * @return 若成功初始化并绑定上下文返回 true；若系统缺少 ANGLE/EGL 驱动返回 false 并平滑跳过
     */
    bool initialize(uint32_t width = 64, uint32_t height = 64);

    /**
     * @brief 释放离屏上下文与 EGL 显示资源
     */
    void terminate();

    /**
     * @brief 查询当前无头硬件上下文是否可用
     */
    bool isAvailable() const noexcept { return m_isAvailable; }

    /**
     * @brief 获取离屏缓冲宽度
     */
    uint32_t getWidth() const noexcept { return m_width; }

    /**
     * @brief 获取离屏缓冲高度
     */
    uint32_t getHeight() const noexcept { return m_height; }

private:
    bool m_isAvailable{false};
    uint32_t m_width{0};
    uint32_t m_height{0};

    void* m_eglModule{nullptr};
    void* m_glesModule{nullptr};
    void* m_eglDisplay{nullptr};
    void* m_eglSurface{nullptr};
    void* m_eglContext{nullptr};
};

} // namespace MQEngine::Testing
