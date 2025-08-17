#ifndef UNIFORM_H
#define UNIFORM_H
#include <iostream>

namespace MQEngine
{
    struct Uniform
    {
    public:
        Uniform(): m_uniformBuffer(nullptr), m_constBuffer(nullptr)
        {
        }
        Uniform(Uniform&& other) noexcept
          : m_uniformBuffer(other.m_uniformBuffer), m_constBuffer(other.m_constBuffer)
        {
            other.m_uniformBuffer = nullptr;
            other.m_constBuffer = nullptr;
        }
        Uniform& operator=(Uniform&& other) noexcept
        {
            if (this != &other) {
                if (m_uniformBuffer)
                {
                    delete m_uniformBuffer;
                    std::cout << "real delete" << std::endl;
                }
                if (m_constBuffer)
                {
                    delete m_constBuffer;
                    std::cout << "real delete" << std::endl;
                }

                m_uniformBuffer = other.m_uniformBuffer;
                m_constBuffer = other.m_constBuffer;

                other.m_uniformBuffer = nullptr;
                other.m_constBuffer = nullptr;
            }
            return *this;
        }



        Uniform(FCT::Context* ctx,FCT::ConstLayout layout)
        {
            m_uniformBuffer = new FCT::UniformBuffer(layout);
            m_constBuffer = ctx->createResource<FCT::RHI::ConstBuffer>();
            m_constBuffer->layout(layout);
            m_constBuffer->buffer(m_uniformBuffer);
            m_constBuffer->create();
        }
        ~Uniform()
        {
            if (m_uniformBuffer)
            {
                delete m_uniformBuffer;
                std::cout << "real delete" << std::endl;
            }
            if (m_constBuffer)
            {
                delete m_constBuffer;
                std::cout << "real delete" << std::endl;
            }
            std::cout << "delete" << std::endl;
        }
        template<typename T>
        void setValue(const char* name, const T& value)
        {
            m_uniformBuffer->setValue(name, value);
        }
        void update()
        {
            m_constBuffer->updataData();
        }
        operator FCT::RHI::ConstBuffer*() const
        {
            return m_constBuffer;
        }
    private:
        FCT::UniformBuffer* m_uniformBuffer;
        FCT::RHI::ConstBuffer* m_constBuffer;
    };
}

#endif //UNIFORM_H
