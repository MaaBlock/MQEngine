#ifndef TECH_H
#define TECH_H
#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <set>
#include <algorithm>
#include "../thirdparty/thirdparty.h"
#include "../system/BindedSystem.h"

namespace MQEngine
{
    class DataManager;
    
    struct EntityRenderContext {
        entt::registry& registry;
        entt::entity entity;
        FCT::Layout* layout;
        FCT::RHI::CommandBuffer* cmdBuf;
        const std::string& techName;
        const std::string& passName;
    };

    /**
     * @brief 实体操作回调函数类型
     * @param context 渲染上下文
     */
    using EntityOperationCallback = std::function<void(const EntityRenderContext& context)>;

    /**
     * @brief 定义Tech的名称
     */
    struct TechName
    {
        std::string name;
    };

    /**
     * @brief 定义顶点着色器源码
     */
    struct VertexShaderSource
    {
        std::string source;
    };

    /**
     * @brief 定义像素着色器源码
     */
    struct PixelShaderSource
    {
        std::string source;
    };

    /**
     * @brief 定义组件过滤器，用于runtime view
     */
    struct ComponentFilter
    {
        std::vector<entt::type_info> include_types;  // 必须包含的组件类型
        std::vector<entt::type_info> exclude_types;  // 必须排除的组件类型

        template<typename... Components>
        ComponentFilter& include()
        {
            (include_types.push_back(entt::type_id<Components>()), ...);
            return *this;
        }

        template<typename... Components>
        ComponentFilter& exclude()
        {
            (exclude_types.push_back(entt::type_id<Components>()), ...);
            return *this;
        }
    };
    class Tech
    {
    public:
        struct ImageLinked
        {
            struct Source
            {
                enum class Type
                {
                    FromPass
                };
                std::string name;
            } source;
            struct Slot
            {
                std::string name;
            };
        };
    public:

        /** @name 构造与配置
         *  @{
         */
        template<typename... Args>
        Tech(Args... args);
        

        /** @} */

        /** @name 访问器
         *  @{
         */
        const std::string& getName() const { return m_name; }
        const std::string& getVertexShaderSource() const { return m_vsSource; }
        const std::string& getPixelShaderSource() const { return m_psSource; }
        const std::vector<FCT::VertexLayout>& getVertexLayouts() const { return m_vertexLayouts; }
        const FCT::PixelLayout& getPixelLayout() const { return m_pixelLayout; }
        const std::vector<FCT::UniformSlot>& getUniformSlots() const { return m_uniformSlots; }
        const std::vector<FCT::SamplerSlot>& getSamplerSlots() const { return m_samplerSlots; }
        const std::vector<FCT::TextureSlot>& getTextureSlots() const { return m_textureSlots; }
        const std::string& getPassName() const { return m_passName; }
        const ShaderRef& getVertexShaderRef() const { return m_vsRef; }
        const ShaderRef& getPixelShaderRef() const { return m_psRef; }
        const ComponentFilter& getComponentFilter() const { return m_componentFilter; }
        /** @} */

        /** @name 实体操作回调
         *  @{
         */
        void executeEntityOperationCallback(entt::registry& registry, entt::entity entity, FCT::Layout* layout, FCT::RHI::CommandBuffer* cmdBuf) const {
            if (m_entityOperationCallback) {
                EntityRenderContext context{registry, entity, layout, cmdBuf, m_name, m_passName};
                m_entityOperationCallback(context);
            }
        }
        /** @} */

        /** @name 内部设置器（供TechManager使用）
         *  @{
         */
        void setPassName(const std::string& passName) { m_passName = passName; }
        void setVertexShaderRef(const ShaderRef& ref) { m_vsRef = ref; }
        void setPixelShaderRef(const ShaderRef& ref) { m_psRef = ref; }
        void setEntityOperationCallback(const EntityOperationCallback& callback) { m_entityOperationCallback = callback; }
        /** @} */
        
        /** @name 回调执行
         *  @{
         */
        /** @} */
        Status valid()
        {
            if (m_passName.empty())
                return InvalidArgumentError("Tech的pass名称为空");
            if (m_vsSource.empty())
                return InvalidArgumentError("Tech的顶点着色器为空");
            if (m_vertexLayouts.empty())
                return InvalidArgumentError("Tech的顶点布局为空");

            return OkStatus();
        }
        std::vector<BindedSystem*> getSystems() const
        {
            return m_bindedSystems;
        }
    private:
        // --- 构造函数参数处理 ---
        void processArgs() {} // 终止递归
        
        template<typename... Args>
        void processArgs(const TechName& name, Args... args);
        
        template<typename... Args>
        void processArgs(const VertexShaderSource& vs, Args... args);
        
        template<typename... Args>
        void processArgs(const PixelShaderSource& ps, Args... args);
        
        template<typename... Args>
        void processArgs(const FCT::VertexLayout& layout, Args... args);
        
        template<typename... Args>
        void processArgs(const std::vector<FCT::VertexLayout>& layouts, Args... args);
        
        template<typename... Args>
        void processArgs(const FCT::PixelLayout& layout, Args... args);
        
        template<typename... Args>
        void processArgs(const FCT::UniformSlot& slot, Args... args);
        
        template<typename... Args>
        void processArgs(const std::vector<FCT::UniformSlot>& slots, Args... args);
        
        template<typename... Args>
        void processArgs(const FCT::SamplerSlot& slot, Args... args);
        
        template<typename... Args>
        void processArgs(const std::vector<FCT::SamplerSlot>& slots, Args... args);
        
        template<typename... Args>
        void processArgs(const FCT::TextureSlot& slot, Args... args);
        
        template<typename... Args>
        void processArgs(const std::vector<FCT::TextureSlot>& slots, Args... args);
        
        template<typename... Args>
        void processArgs(const ComponentFilter& filter, Args... args);
        
        template<typename... Args>
        void processArgs(const EntityOperationCallback& callback, Args... args);


        template<typename... Args>
        void processArgs(const ImageLinked& linked, Args... args);

        template<typename... Args>
        void processArgs(const std::vector<ImageLinked>& linked, Args... args);

        template<typename... Args>
        void processArgs(BindedSystem* system, Args... args);


        // --- 成员变量 ---
        std::string m_name;
        std::string m_vsSource;
        std::string m_psSource;
        std::vector<FCT::VertexLayout> m_vertexLayouts;
        FCT::PixelLayout m_pixelLayout;
        std::vector<FCT::UniformSlot> m_uniformSlots;
        std::vector<FCT::SamplerSlot> m_samplerSlots;
        std::vector<FCT::TextureSlot> m_textureSlots;
        std::vector<ImageLinked> m_imageLinks;
        ShaderRef m_vsRef;
        ShaderRef m_psRef;
        std::string m_passName;
        ComponentFilter m_componentFilter;
        EntityOperationCallback m_entityOperationCallback;  // 实体操作回调函数
        /**
         * @brief 绑定的 系统，会从这里面获取slot，和对应的bindable
         */
        std::vector<BindedSystem*> m_bindedSystems;
    };
}
#include "./Tech.hpp"
#endif
