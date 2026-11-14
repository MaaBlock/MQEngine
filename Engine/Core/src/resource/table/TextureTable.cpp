#include <MQEngine/resource/table/TextureTable.h>

namespace MQEngine
{
    namespace Resource
    {
        TextureTable::TextureTable() = default;

        TextureTable::~TextureTable()
        {
            clear();
        }

        void TextureTable::registerTexture(const void* handle, TextureRecord record)
        {
            if (!handle) {
                return;
            }
            record.handle = handle;
            const uint64_t bytes = record.vramBytes;
            m_table.registerResource(handle, std::move(record));
            m_totalVramBytes.fetch_add(bytes, std::memory_order_relaxed);
        }

        void TextureTable::unregisterTexture(const void* handle)
        {
            if (!handle) {
                return;
            }
            if (const auto* rec = getTexture(handle)) {
                m_totalVramBytes.fetch_sub(rec->vramBytes, std::memory_order_relaxed);
            }
            m_table.unregisterResource(handle);
        }

        const TextureRecord* TextureTable::getTexture(const void* handle) const noexcept
        {
            return m_table.findResource(handle);
        }

        TextureRecord* TextureTable::getTexture(const void* handle) noexcept
        {
            return m_table.findResource(handle);
        }

        bool TextureTable::contains(const void* handle) const noexcept
        {
            return m_table.contains(handle);
        }

        uint32_t TextureTable::allocateBindlessSlot(const void* handle)
        {
            auto* rec = getTexture(handle);
            if (!rec) {
                return UINT32_MAX;
            }
            if (rec->bindlessSlot != UINT32_MAX) {
                return rec->bindlessSlot;
            }
            uint32_t slot = m_nextBindlessSlot.fetch_add(1, std::memory_order_relaxed);
            rec->bindlessSlot = slot;
            return slot;
        }

        uint32_t TextureTable::getBindlessSlot(const void* handle) const noexcept
        {
            const auto* rec = getTexture(handle);
            return rec ? rec->bindlessSlot : UINT32_MAX;
        }

        void TextureTable::setDefaultFallback(const void* fallbackHandle) noexcept
        {
            m_defaultFallback.store(fallbackHandle, std::memory_order_release);
        }

        const void* TextureTable::getDefaultFallback() const noexcept
        {
            return m_defaultFallback.load(std::memory_order_acquire);
        }

        const void* TextureTable::resolveHandle(const void* handle) const noexcept
        {
            if (handle && contains(handle)) {
                return handle;
            }
            return getDefaultFallback();
        }

        void TextureTable::clear() noexcept
        {
            m_table.clear();
            m_totalVramBytes.store(0, std::memory_order_relaxed);
            m_nextBindlessSlot.store(0, std::memory_order_relaxed);
        }

    } // namespace Resource
} // namespace MQEngine
