#ifndef UNIFORM_H
#define UNIFORM_H

namespace MQEngine
{
    struct Uniform
    {
    public:
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
            delete m_uniformBuffer;
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
