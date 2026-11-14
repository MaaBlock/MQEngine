#include "HeadlessGlesFixture.h"
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace MQEngine::Testing {

HeadlessGlesFixture::HeadlessGlesFixture() = default;

HeadlessGlesFixture::~HeadlessGlesFixture() {
    terminate();
}

bool HeadlessGlesFixture::initialize(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

#ifdef _WIN32
    // 动态探测 ANGLE / EGL 运行时动态库
    HMODULE eglMod = LoadLibraryA("libEGL.dll");
    if (!eglMod) {
        eglMod = LoadLibraryA("libEGL.dll.1");
    }
    if (!eglMod) {
        m_isAvailable = false;
        std::cout << "[HeadlessGlesFixture] 未检测到 ANGLE libEGL.dll 动态库，驱动级硬件测试将优雅跳过。" << std::endl;
        return false;
    }

    HMODULE glesMod = LoadLibraryA("libGLESv2.dll");
    m_eglModule = reinterpret_cast<void*>(eglMod);
    m_glesModule = reinterpret_cast<void*>(glesMod);

    // 尝试动态解析 EGL 函数指针
    using PFN_eglGetDisplay = void* (__stdcall*)(void*);
    using PFN_eglInitialize = int (__stdcall*)(void*, int*, int*);
    using PFN_eglTerminate = int (__stdcall*)(void*);

    auto pfnGetDisplay = reinterpret_cast<PFN_eglGetDisplay>(GetProcAddress(eglMod, "eglGetDisplay"));
    auto pfnInitialize = reinterpret_cast<PFN_eglInitialize>(GetProcAddress(eglMod, "eglInitialize"));

    if (!pfnGetDisplay || !pfnInitialize) {
        m_isAvailable = false;
        std::cout << "[HeadlessGlesFixture] libEGL.dll 导出符号不完整，优雅跳过。" << std::endl;
        return false;
    }

    void* display = pfnGetDisplay(nullptr); // EGL_DEFAULT_DISPLAY
    if (!display) {
        m_isAvailable = false;
        std::cout << "[HeadlessGlesFixture] eglGetDisplay 返回空，优雅跳过。" << std::endl;
        return false;
    }

    int major = 0, minor = 0;
    if (!pfnInitialize(display, &major, &minor)) {
        m_isAvailable = false;
        std::cout << "[HeadlessGlesFixture] eglInitialize 初始化失败，优雅跳过。" << std::endl;
        return false;
    }

    m_eglDisplay = display;
    m_isAvailable = true;
    std::cout << "[HeadlessGlesFixture] 成功初始化 ANGLE EGL " << major << "." << minor << " 离屏上下文。" << std::endl;
    return true;
#else
    m_isAvailable = false;
    std::cout << "[HeadlessGlesFixture] 非 Windows 环境当前未配置 ANGLE，优雅跳过。" << std::endl;
    return false;
#endif
}

void HeadlessGlesFixture::terminate() {
#ifdef _WIN32
    if (m_eglDisplay && m_eglModule) {
        using PFN_eglTerminate = int (__stdcall*)(void*);
        auto pfnTerminate = reinterpret_cast<PFN_eglTerminate>(
            GetProcAddress(reinterpret_cast<HMODULE>(m_eglModule), "eglTerminate")
        );
        if (pfnTerminate) {
            pfnTerminate(m_eglDisplay);
        }
        m_eglDisplay = nullptr;
    }

    if (m_glesModule) {
        FreeLibrary(reinterpret_cast<HMODULE>(m_glesModule));
        m_glesModule = nullptr;
    }
    if (m_eglModule) {
        FreeLibrary(reinterpret_cast<HMODULE>(m_eglModule));
        m_eglModule = nullptr;
    }
#endif
    m_isAvailable = false;
}

} // namespace MQEngine::Testing
