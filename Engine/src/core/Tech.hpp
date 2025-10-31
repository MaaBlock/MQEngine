#ifndef TECH_HPP
#define TECH_HPP
#include "Tech.h"

namespace MQEngine
{
    template<typename... Args>
    Tech::Tech(Args... args)
    {
        processArgs(args...);
    }



    template<typename... Args>
    void Tech::processArgs(const TechName& name, Args... args)
    {
        m_name = name.name;
        processArgs(args...);
    }

    template<typename... Args>
    void Tech::processArgs(const VertexShaderSource& vs, Args... args)
    {
        m_vsSource = vs.source;
        processArgs(args...);
    }

    template<typename... Args>
    void Tech::processArgs(const PixelShaderSource& ps, Args... args)
    {
        m_psSource = ps.source;
        processArgs(args...);
    }

    template<typename... Args>
    void Tech::processArgs(const FCT::VertexLayout& layout, Args... args)
    {
        m_vertexLayouts.push_back(layout);
        processArgs(args...);
    }

    template<typename... Args>
    void Tech::processArgs(const std::vector<FCT::VertexLayout>& layouts, Args... args)
    {
        for (const auto& layout : layouts)
        {
            m_vertexLayouts.push_back(layout);
        }
        processArgs(args...);
    }

    template<typename... Args>
    void Tech::processArgs(const FCT::PixelLayout& layout, Args... args)
    {
        m_pixelLayout = layout;
        processArgs(args...);
    }

    template<typename... Args>
    void Tech::processArgs(const FCT::UniformSlot& slot, Args... args)
    {
        m_uniformSlots.push_back(slot);
        processArgs(args...);
    }

    template<typename... Args>
    void Tech::processArgs(const std::vector<FCT::UniformSlot>& slots, Args... args)
    {
        for (const auto& slot : slots)
        {
            m_uniformSlots.push_back(slot);
        }
        processArgs(args...);
    }

    template<typename... Args>
    void Tech::processArgs(const FCT::SamplerSlot& slot, Args... args)
    {
        m_samplerSlots.push_back(slot);
        processArgs(args...);
    }

    template<typename... Args>
    void Tech::processArgs(const std::vector<FCT::SamplerSlot>& slots, Args... args)
    {
        for (const auto& slot : slots)
        {
            m_samplerSlots.push_back(slot);
        }
        processArgs(args...);
    }

    template<typename... Args>
    void Tech::processArgs(const FCT::TextureSlot& slot, Args... args)
    {
        m_textureSlots.push_back(slot);
        processArgs(args...);
    }

    template<typename... Args>
    void Tech::processArgs(const std::vector<FCT::TextureSlot>& slots, Args... args)
    {
        for (const auto& slot : slots)
        {
            m_textureSlots.push_back(slot);
        }
        processArgs(args...);
    }

    template<typename... Args>
    void Tech::processArgs(const ComponentFilter& filter, Args... args)
    {
        m_componentFilter = filter;
        processArgs(args...);
    }
    
    template<typename... Args>
    void Tech::processArgs(const TechBindCallback& callback, Args... args)
    {
        m_bindCallback = callback;
        processArgs(args...);
    }

    template <typename... Args>
    void Tech::processArgs(const EntityOperationCallback& callback, Args... args)
    {
        m_entityOperationCallback = callback;
        processArgs(args...);
    }
    template <typename... Args>
    void Tech::processArgs(const ImageLinked& linked, Args... args)
    {
        m_imageLinks.push_back(linked);
    }
    template <typename... Args>
    void Tech::processArgs(const std::vector<ImageLinked>& linked, Args... args)
    {
        for (const auto& link : linked)
        {
            m_imageLinks.push_back(link);
        }
    }

} // namespace MQEngine

#endif // TECH_HPP