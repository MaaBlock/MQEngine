#include <MQEngine/resource/table/PipelineTable.h>

namespace MQEngine
{
    namespace Resource
    {
        PipelineTable::PipelineTable() = default;

        PipelineTable::~PipelineTable()
        {
            clear();
        }

        void PipelineTable::registerPipeline(const void* handle, PipelineRecord record)
        {
            if (!handle) {
                return;
            }
            record.handle = handle;
            m_table.registerResource(handle, std::move(record));
        }

        void PipelineTable::unregisterPipeline(const void* handle)
        {
            if (!handle) {
                return;
            }
            m_table.unregisterResource(handle);
        }

        const PipelineRecord* PipelineTable::getPipeline(const void* handle) const noexcept
        {
            return m_table.findResource(handle);
        }

        PipelineRecord* PipelineTable::getPipeline(const void* handle) noexcept
        {
            return m_table.findResource(handle);
        }

        bool PipelineTable::contains(const void* handle) const noexcept
        {
            return m_table.contains(handle);
        }

        void PipelineTable::setDefaultFallback(const void* fallbackHandle) noexcept
        {
            m_defaultFallback.store(fallbackHandle, std::memory_order_release);
        }

        const void* PipelineTable::getDefaultFallback() const noexcept
        {
            return m_defaultFallback.load(std::memory_order_acquire);
        }

        const void* PipelineTable::resolveHandle(const void* handle) const noexcept
        {
            if (handle && contains(handle)) {
                return handle;
            }
            return getDefaultFallback();
        }

        void PipelineTable::clear() noexcept
        {
            m_table.clear();
        }

    } // namespace Resource
} // namespace MQEngine
