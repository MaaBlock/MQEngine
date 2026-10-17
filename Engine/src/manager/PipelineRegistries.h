#ifndef ENGINE_PIPELINEREGISTRIES_H
#define ENGINE_PIPELINEREGISTRIES_H

#include "../thirdparty/thirdparty.h"
#include "../core/tech.h"
#include <string>
#include <unordered_map>

namespace MQEngine {

/**
 * @brief 组件类型注册中心，负责将配置中的字符串名称映射到 entt::type_info
 */
class ENGINE_API ComponentRegistry {
public:
    static ComponentRegistry& instance();

    ComponentRegistry();
    virtual ~ComponentRegistry() = default;

    void registerComponent(const std::string& name, entt::type_info info);

    template <typename T>
    void registerComponent(const std::string& name) {
        registerComponent(name, entt::type_id<T>());
    }

    StatusOr<entt::type_info> getTypeInfo(const std::string& name) const;
    entt::id_type getTypeId(const std::string& name) const;
    bool hasComponent(const std::string& name) const;

    void registerAllStandardComponents();

private:
    std::unordered_map<std::string, entt::type_info> m_components;
};

/**
 * @brief 统一槽位注册中心，负责将字符串名称映射到 FCT::UniformSlot
 */
class ENGINE_API UniformSlotRegistry {
public:
    static UniformSlotRegistry& instance();

    UniformSlotRegistry();
    virtual ~UniformSlotRegistry() = default;

    void registerSlot(const std::string& name, const FCT::UniformSlot& slot);
    StatusOr<FCT::UniformSlot> getSlot(const std::string& name) const;
    bool hasSlot(const std::string& name) const;

    void registerAllStandardSlots();

private:
    std::unordered_map<std::string, FCT::UniformSlot> m_slots;
};

/**
 * @brief 实体绘制回调注册中心，负责将字符串名称映射到 EntityOperationCallback
 */
class ENGINE_API EntityCallbackRegistry {
public:
    static EntityCallbackRegistry& instance();

    EntityCallbackRegistry();
    virtual ~EntityCallbackRegistry() = default;

    void registerCallback(const std::string& name, EntityOperationCallback callback);
    StatusOr<EntityOperationCallback> getCallback(const std::string& name) const;
    bool hasCallback(const std::string& name) const;

    void registerAllStandardCallbacks();

private:
    std::unordered_map<std::string, EntityOperationCallback> m_callbacks;
};

/**
 * @brief 顶点与像素布局注册中心
 */
class ENGINE_API LayoutRegistry {
public:
    static LayoutRegistry& instance();

    LayoutRegistry();
    virtual ~LayoutRegistry() = default;

    void registerVertexLayout(const std::string& name, const FCT::VertexLayout& layout);
    StatusOr<FCT::VertexLayout> getVertexLayout(const std::string& name) const;

    void registerPixelLayout(const std::string& name, const FCT::PixelLayout& layout);
    StatusOr<FCT::PixelLayout> getPixelLayout(const std::string& name) const;

    void registerAllStandardLayouts();

private:
    std::unordered_map<std::string, FCT::VertexLayout> m_vertexLayouts;
    std::unordered_map<std::string, FCT::PixelLayout> m_pixelLayouts;
};

/**
 * @brief 着色器源码注册中心
 */
class ENGINE_API ShaderSourceRegistry {
public:
    static ShaderSourceRegistry& instance();

    ShaderSourceRegistry();
    virtual ~ShaderSourceRegistry() = default;

    void registerShader(const std::string& name, const std::string& source);
    StatusOr<std::string> getShader(const std::string& name) const;
    bool hasShader(const std::string& name) const;

    void registerAllStandardShaders();

private:
    std::unordered_map<std::string, std::string> m_shaders;
};

/**
 * @brief 初始化所有引擎内置标准注册项
 */
ENGINE_API void InitializePipelineRegistries();

} // namespace MQEngine

#endif // ENGINE_PIPELINEREGISTRIES_H
