/**
 * @file PhysicsCommon.cpp
 * @brief PhysX 5.5.0 核心基础单例实现
 */
#include "PhysicsCommon.h"
#include <algorithm>
#include <spdlog/spdlog.h>

#if defined(_WIN32)
#include <malloc.h>
#else
#include <stdlib.h>
#endif

namespace MQEngine {

    void PhysicsErrorCallback::reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line)
    {
        switch (code) {
            case physx::PxErrorCode::eNO_ERROR:
                spdlog::info("[PhysX] {}:{} -> {}", file, line, message);
                break;
            case physx::PxErrorCode::eDEBUG_INFO:
                spdlog::debug("[PhysX Info] {}:{} -> {}", file, line, message);
                break;
            case physx::PxErrorCode::eDEBUG_WARNING:
            case physx::PxErrorCode::ePERF_WARNING:
                spdlog::warn("[PhysX Warning {}] {}:{} -> {}", static_cast<int>(code), file, line, message);
                break;
            default:
                spdlog::error("[PhysX Error {}] {}:{} -> {}", static_cast<int>(code), file, line, message);
                break;
        }
    }

    void* PhysicsAllocatorCallback::allocate(size_t size, const char* typeName, const char* filename, int line)
    {
        (void)typeName;
        (void)filename;
        (void)line;
#if defined(_WIN32)
        return _aligned_malloc(size, 16);
#else
        void* ptr = nullptr;
        posix_memalign(&ptr, 16, size);
        return ptr;
#endif
    }

    void PhysicsAllocatorCallback::deallocate(void* ptr)
    {
        if (!ptr) {
            return;
        }
#if defined(_WIN32)
        _aligned_free(ptr);
#else
        free(ptr);
#endif
    }

    PhysicsCommon& PhysicsCommon::get()
    {
        static PhysicsCommon s_instance;
        return s_instance;
    }

    PhysicsCommon::~PhysicsCommon()
    {
        shutdown();
    }

    bool PhysicsCommon::init()
    {
        if (m_initialized) {
            return true;
        }

        m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_allocatorCallback, m_errorCallback);
        if (!m_foundation) {
            spdlog::error("[PhysicsCommon] 创建 PxFoundation 失败 (版本: {})", PX_PHYSICS_VERSION);
            return false;
        }

        m_tolerancesScale.length = 1.0f;
        m_tolerancesScale.speed = 10.0f;

        m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, m_tolerancesScale, true, nullptr);
        if (!m_physics) {
            spdlog::error("[PhysicsCommon] 创建 PxPhysics 实例失败");
            m_foundation->release();
            m_foundation = nullptr;
            return false;
        }

        if (!PxInitExtensions(*m_physics, nullptr)) {
            spdlog::error("[PhysicsCommon] 初始化 PhysX 扩展库失败");
            m_physics->release();
            m_physics = nullptr;
            m_foundation->release();
            m_foundation = nullptr;
            return false;
        }

        m_defaultMaterial = m_physics->createMaterial(0.5f, 0.5f, 0.5f);
        if (!m_defaultMaterial) {
            spdlog::warn("[PhysicsCommon] 创建默认 PxMaterial 失败");
        }

        m_initialized = true;
        spdlog::info("[PhysicsCommon] PhysX 5.5.0 初始化成功");
        return true;
    }

    void PhysicsCommon::shutdown()
    {
        if (!m_initialized) {
            return;
        }

        // Note: 严格按照 PhysX 逆序安全释放
        if (m_defaultMaterial) {
            m_defaultMaterial->release();
            m_defaultMaterial = nullptr;
        }

        PxCloseExtensions();

        if (m_physics) {
            m_physics->release();
            m_physics = nullptr;
        }

        if (m_foundation) {
            m_foundation->release();
            m_foundation = nullptr;
        }

        m_initialized = false;
        spdlog::info("[PhysicsCommon] PhysX 5.5.0 彻底释放完毕");
    }

    physx::PxMaterial* PhysicsCommon::createMaterial(float staticFriction, float dynamicFriction, float restitution)
    {
        if (!m_physics) {
            return nullptr;
        }
        return m_physics->createMaterial(
            (std::max)(0.0f, staticFriction),
            (std::max)(0.0f, dynamicFriction),
            std::clamp(restitution, 0.0f, 1.0f)
        );
    }

} // namespace MQEngine
