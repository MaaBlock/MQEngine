#include <MQEngine/resource/table/MaterialTable.h>

namespace MQEngine
{
    namespace Resource
    {
        MaterialTable::MaterialTable() = default;

        MaterialTable::~MaterialTable()
        {
            clear();
        }

        void MaterialTable::registerMaterial(const void* handle, MaterialRecord record)
        {
            if (!handle) {
                return;
            }
            record.handle = handle;
            const uint64_t bytes = record.vramBytes;
            m_table.registerResource(handle, std::move(record));
            m_totalVramBytes.fetch_add(bytes, std::memory_order_relaxed);
        }

        void MaterialTable::unregisterMaterial(const void* handle)
        {
            if (!handle) {
                return;
            }
            if (const auto* rec = getMaterial(handle)) {
                m_totalVramBytes.fetch_sub(rec->vramBytes, std::memory_order_relaxed);
            }
            m_table.unregisterResource(handle);
        }

        const MaterialRecord* MaterialTable::getMaterial(const void* handle) const noexcept
        {
            return m_table.findResource(handle);
        }

        MaterialRecord* MaterialTable::getMaterial(const void* handle) noexcept
        {
            return m_table.findResource(handle);
        }

        bool MaterialTable::contains(const void* handle) const noexcept
        {
            return m_table.contains(handle);
        }

        void MaterialTable::setDefaultFallback(const void* fallbackHandle) noexcept
        {
            m_defaultFallback.store(fallbackHandle, std::memory_order_release);
        }

        const void* MaterialTable::getDefaultFallback() const noexcept
        {
            return m_defaultFallback.load(std::memory_order_acquire);
        }

        const void* MaterialTable::resolveHandle(const void* handle) const noexcept
        {
            if (handle && contains(handle)) {
                return handle;
            }
            return getDefaultFallback();
        }

        void MaterialTable::clear() noexcept
        {
            m_table.clear();
            m_totalVramBytes.store(0, std::memory_order_relaxed);
        }

    } // namespace Resource
} // namespace MQEngine
