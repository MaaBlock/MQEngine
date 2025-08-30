
#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include "../Thirdparty/thirdparty.h"

namespace MQEngine
{
    class DataManager;

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

    // Tech 定义了一套完整的渲染技术，使用声明式接口
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
        /** @} */

        /** @name 内部设置器（供TechManager使用）
         *  @{
         */
        void setPassName(const std::string& passName) { m_passName = passName; }
        void setVertexShaderRef(const ShaderRef& ref) { m_vs_ref = ref; }
        void setPixelShaderRef(const ShaderRef& ref) { m_ps_ref = ref; }
        /** @} */

    private:
        // --- 构造函数参数处理 ---
        template<typename... Args>
        void processArgs(Args... args);
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
    };

    class TechManager
    {
    public:
        TechManager();

        void addTech(const std::string& passName, Tech&& tech);

        const std::vector<Tech*>& getTechsForPass(const std::string& passName);

        FCT::Layout* getLayoutForTech(const std::string& techName);

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
        DataManager* m_dataManager;
    };
}
