#include <MQEngine/resource/table/ShaderTable.h>

namespace MQEngine
{
    namespace Resource
    {
        ShaderTable::ShaderTable() = default;

        ShaderTable::~ShaderTable()
        {
            clear();
        }

        void ShaderTable::registerShader(const void* handle, ShaderRecord record)
        {
            if (!handle) {
                return;
            }
            record.handle = handle;
            m_table.registerResource(handle, std::move(record));
        }

        void ShaderTable::unregisterShader(const void* handle)
        {
            if (!handle) {
                return;
            }
            m_table.unregisterResource(handle);
        }

        const ShaderRecord* ShaderTable::getShader(const void* handle) const noexcept
        {
            return m_table.findResource(handle);
        }

        ShaderRecord* ShaderTable::getShader(const void* handle) noexcept
        {
            return m_table.findResource(handle);
        }

        bool ShaderTable::contains(const void* handle) const noexcept
        {
            return m_table.contains(handle);
        }

        void ShaderTable::setDefaultFallback(const void* fallbackHandle) noexcept
        {
            m_defaultFallback.store(fallbackHandle, std::memory_order_release);
        }

        const void* ShaderTable::getDefaultFallback() const noexcept
        {
            return m_defaultFallback.load(std::memory_order_acquire);
        }

        const void* ShaderTable::resolveHandle(const void* handle) const noexcept
        {
            if (handle && contains(handle)) {
                return handle;
            }
            return getDefaultFallback();
        }

        void ShaderTable::clear() noexcept
        {
            m_table.clear();
        }

    } // namespace Resource
} // namespace MQEngine
