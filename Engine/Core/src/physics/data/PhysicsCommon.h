/**
 * @file PhysicsCommon.h
 * @brief PhysX 5.5.0 核心基础单例与全局环境管理
 */
#ifndef ENGINE_PHYSICS_COMMON_H
#define ENGINE_PHYSICS_COMMON_H

#include "../../EnginePCH.h"
#include "../../thirdparty/thirdparty.h"

namespace MQEngine {

    /**
     * @brief 自定义 PhysX 错误回调，将 PhysX 核心警告与错误统一路由至 spdlog
     */
    class PhysicsErrorCallback : public physx::PxErrorCallback {
    public:
        virtual ~PhysicsErrorCallback() = default;
        virtual void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override;
    };

    /**
     * @brief 自定义 16 字节对齐内存分配器回调
     */
    class PhysicsAllocatorCallback : public physx::PxAllocatorCallback {
    public:
        virtual ~PhysicsAllocatorCallback() = default;
        virtual void* allocate(size_t size, const char* typeName, const char* filename, int line) override;
        virtual void deallocate(void* ptr) override;
    };

    /**
     * @brief PhysX 基础运行库与单例管理器
     * 负责 PxFoundation、PxPhysics、默认物理材质及 PhysX 扩展库的生命周期管理
     */
    class ENGINE_API PhysicsCommon {
    public:
        /**
         * @brief 获取全局单例引用
         */
        static PhysicsCommon& get();

        PhysicsCommon(const PhysicsCommon&) = delete;
        PhysicsCommon& operator=(const PhysicsCommon&) = delete;
        PhysicsCommon(PhysicsCommon&&) = delete;
        PhysicsCommon& operator=(PhysicsCommon&&) = delete;

        /**
         * @brief 初始化 PhysX 核心单例 (Foundation, Physics, Extensions, 默认材质)
         * @return 成功返回 true，若已初始化则幂等返回 true
         */
        bool init();

        /**
         * @brief 彻底关闭并释放 PhysX 基础核心资源
         * Note: 必须在所有 PhysicsScene 析构完成后调用，严格遵循逆序释放
         */
        void shutdown();

        /**
         * @brief 检查 PhysX 基础核心是否已成功初始化
         */
        bool isInitialized() const { return m_initialized; }

        /**
         * @brief 获取 PhysX 顶层工厂接口
         */
        physx::PxPhysics* getPhysics() const { return m_physics; }

        /**
         * @brief 获取 PhysX Foundation 实例
         */
        physx::PxFoundation* getFoundation() const { return m_foundation; }

        /**
         * @brief 获取全局物理容差尺度
         */
        const physx::PxTolerancesScale& getTolerancesScale() const { return m_tolerancesScale; }

        /**
         * @brief 获取全局默认物理材质
         */
        physx::PxMaterial* getDefaultMaterial() const { return m_defaultMaterial; }

        /**
         * @brief 创建新的物理材质
         * @param staticFriction 静摩擦系数 (>= 0.0f)
         * @param dynamicFriction 动摩擦系数 (>= 0.0f)
         * @param restitution 恢复弹性系数 (0.0f - 1.0f)
         * @return 返回创建成功的 PxMaterial 指针，失败返回 nullptr
         */
        physx::PxMaterial* createMaterial(float staticFriction, float dynamicFriction, float restitution);

    private:
        PhysicsCommon() = default;
        ~PhysicsCommon();

        bool m_initialized = false;
        PhysicsAllocatorCallback m_allocatorCallback;
        PhysicsErrorCallback m_errorCallback;

        physx::PxFoundation* m_foundation = nullptr;
        physx::PxPhysics* m_physics = nullptr;
        physx::PxTolerancesScale m_tolerancesScale;
        physx::PxMaterial* m_defaultMaterial = nullptr;
    };

    /**
     * @brief 将 entt::entity 编码为 PhysX userData 指针 (+1 偏移避免 entity 0 被误判为 nullptr)
     */
    inline void* encodePhysicsUserData(entt::entity entity) {
        if (entity == entt::null) {
            return nullptr;
        }
        return reinterpret_cast<void*>(static_cast<uintptr_t>(entity) + 1);
    }

    /**
     * @brief 将 PhysX userData 指针安全解码回 entt::entity
     */
    inline entt::entity decodePhysicsUserData(void* ptr) {
        if (!ptr) {
            return entt::null;
        }
        return static_cast<entt::entity>(reinterpret_cast<uintptr_t>(ptr) - 1);
    }

} // namespace MQEngine

#endif // ENGINE_PHYSICS_COMMON_H
