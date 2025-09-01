#ifndef TECH_H
#define TECH_H
#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <set>
#include "../Thirdparty/thirdparty.h"

namespace MQEngine
{
    class DataManager;
    
    /**
     * @brief Tech绑定回调函数类型
     * @param layout 当前的Layout对象
     * @param techName Tech名称
     * @param passName Pass名称
     */
    using TechBindCallback = std::function<void(FCT::Layout* layout, const std::string& techName, const std::string& passName)>;

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
        const std::string& getVertexShaderSource() const { return m_vs_source; }
        const std::string& getPixelShaderSource() const { return m_ps_source; }
        const std::vector<FCT::VertexLayout>& getVertexLayouts() const { return m_vertexLayouts; }
        const FCT::PixelLayout& getPixelLayout() const { return m_pixelLayout; }
        const std::vector<FCT::UniformSlot>& getUniformSlots() const { return m_uniformSlots; }
        const std::vector<FCT::SamplerSlot>& getSamplerSlots() const { return m_samplerSlots; }
        const std::vector<FCT::TextureSlot>& getTextureSlots() const { return m_textureSlots; }
        const std::string& getPassName() const { return m_passName; }
        const ShaderRef& getVertexShaderRef() const { return m_vs_ref; }
        const ShaderRef& getPixelShaderRef() const { return m_ps_ref; }
        const ComponentFilter& getComponentFilter() const { return m_componentFilter; }
        /** @} */

        /** @name 内部设置器（供TechManager使用）
         *  @{
         */
        void setPassName(const std::string& passName) { m_passName = passName; }
        void setVertexShaderRef(const ShaderRef& ref) { m_vs_ref = ref; }
        void setPixelShaderRef(const ShaderRef& ref) { m_ps_ref = ref; }
        void setBindCallback(const TechBindCallback& callback) { m_bindCallback = callback; }
        /** @} */
        
        /** @name 回调执行
         *  @{
         */
        void executeBindCallback(FCT::Layout* layout) const {
            if (m_bindCallback) {
                m_bindCallback(layout, m_name, m_passName);
            }
        }
        /** @} */

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
        void processArgs(const TechBindCallback& callback, Args... args);

        // --- 成员变量 ---
        std::string m_name;
        std::string m_vs_source;
        std::string m_ps_source;
        std::vector<FCT::VertexLayout> m_vertexLayouts;
        FCT::PixelLayout m_pixelLayout;
        std::vector<FCT::UniformSlot> m_uniformSlots;
        std::vector<FCT::SamplerSlot> m_samplerSlots;
        std::vector<FCT::TextureSlot> m_textureSlots;
        ShaderRef m_vs_ref;
        ShaderRef m_ps_ref;
        std::string m_passName;
        ComponentFilter m_componentFilter;
        TechBindCallback m_bindCallback;  // Tech绑定回调函数
    };

    class TechManager
    {
    public:
        TechManager();

        void addTech(const std::string& passName, Tech&& tech);

        const std::vector<Tech*>& getTechsForPass(const std::string& passName);

        FCT::Layout* getLayoutForTech(const std::string& techName);
        
        /**
         * @brief 为指定Pass订阅渲染事件和PassInfo更新
         * @param passName Pass名称
         */
        void subscribeToPass(const std::string& passName);
        


    private:
        struct LayoutKey
        {
            std::string passName;
            size_t vertexLayoutHash;
            size_t pixelLayoutHash;

            bool operator<(const LayoutKey& other) const
            {
                if (passName < other.passName) return true;
                if (passName > other.passName) return false;
                if (vertexLayoutHash < other.vertexLayoutHash) return true;
                if (vertexLayoutHash > other.vertexLayoutHash) return false;
                return pixelLayoutHash < other.pixelLayoutHash;
            }
        };

        Context* m_ctx;
        std::map<std::string, Tech> m_techs;
        std::map<std::string, std::vector<Tech*>> m_passTechs;
        std::map<LayoutKey, std::unique_ptr<FCT::Layout>> m_layouts;
        std::map<std::string, FCT::Layout*> m_techToLayoutMap;
        std::set<std::string> m_subscribedPasses;  // 跟踪已订阅的Pass
        std::map<std::string, FCT::OutputInfo> m_passOutputInfos;  // 存储各Pass的输出信息
        DataManager* m_dataManager;
    };
}
#endif
