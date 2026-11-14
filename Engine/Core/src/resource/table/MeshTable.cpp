#include <MQEngine/resource/table/MeshTable.h>

namespace MQEngine
{
    namespace Resource
    {
        MeshTable::MeshTable() = default;

        MeshTable::~MeshTable()
        {
            clear();
        }

        void MeshTable::registerMesh(const void* handle, MeshRecord record)
        {
            if (!handle) {
                return;
            }
            record.handle = handle;
            const uint64_t bytes = record.vramBytes;
            m_table.registerResource(handle, std::move(record));
            m_totalVramBytes.fetch_add(bytes, std::memory_order_relaxed);
        }

        void MeshTable::unregisterMesh(const void* handle)
        {
            if (!handle) {
                return;
            }
            if (const auto* rec = getMesh(handle)) {
                m_totalVramBytes.fetch_sub(rec->vramBytes, std::memory_order_relaxed);
            }
            m_table.unregisterResource(handle);
        }

        const MeshRecord* MeshTable::getMesh(const void* handle) const noexcept
        {
            return m_table.findResource(handle);
        }

        MeshRecord* MeshTable::getMesh(const void* handle) noexcept
        {
            return m_table.findResource(handle);
        }

        bool MeshTable::contains(const void* handle) const noexcept
        {
            return m_table.contains(handle);
        }

        void MeshTable::setDefaultFallback(const void* fallbackHandle) noexcept
        {
            m_defaultFallback.store(fallbackHandle, std::memory_order_release);
        }

        const void* MeshTable::getDefaultFallback() const noexcept
        {
            return m_defaultFallback.load(std::memory_order_acquire);
        }

        const void* MeshTable::resolveHandle(const void* handle) const noexcept
        {
            if (handle && contains(handle)) {
                return handle;
            }
            return getDefaultFallback();
        }

        void MeshTable::clear() noexcept
        {
            m_table.clear();
            m_totalVramBytes.store(0, std::memory_order_relaxed);
        }

    } // namespace Resource
} // namespace MQEngine
