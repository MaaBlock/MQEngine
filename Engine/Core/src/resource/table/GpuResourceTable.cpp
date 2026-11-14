#include <MQEngine/resource/table/GpuResourceTable.h>
#include <MQEngine/resource/core/GpuMemoryManager.h>
#include <MQEngine/domain/renderer/BindlessSet0Manager.h>
#include <cassert>

namespace MQEngine
{
    namespace Resource
    {
        GpuResourceTable::GpuResourceTable()
        {
            for (auto& fallback : m_defaultFallbacks) {
                fallback.store(nullptr, std::memory_order_relaxed);
            }
        }

        GpuResourceTable::~GpuResourceTable()
        {
            clear();
        }

        uint32_t GpuResourceTable::acquireBindlessSlot()
        {
            uint32_t slot = UINT32_MAX;
            if (m_freeSlots.pop(slot)) {
                return slot;
            }
            slot = m_nextBindlessSlot.fetch_add(1, std::memory_order_relaxed);
            assert(slot < 4096 && "GpuResourceTable: Bindless 描述符槽位已达 4096 上限");
            return slot;
        }

        void GpuResourceTable::releaseBindlessSlot(uint32_t slot)
        {
            if (slot == UINT32_MAX) {
                return;
            }
            // 重置 GPU 描述符槽位为默认回退纹理，杜绝悬空引用
            if (auto* bindlessMgr = Domain::Renderer::BindlessSet0Manager::InstancePtr()) {
                bindlessMgr->updateTextureSlot(slot, nullptr);
            }
            m_freeSlots.push(slot);
        }

        void GpuResourceTable::registerResource(const void* handle,
                                              GpuResourceType type,
                                              uint64_t vramBytes,
                                              const std::string& debugName,
                                              GpuResourceState state)
        {
            if (!handle) {
                return;
            }

            auto* existing = m_table.findResource(handle);
            if (existing) {
                // 已存在该指针记录，更新 VRAM 与状态增量
                if (vramBytes != existing->vramBytes) {
                    if (vramBytes > existing->vramBytes) {
                        const uint64_t delta = vramBytes - existing->vramBytes;
                        m_totalVramBytes.fetch_add(delta, std::memory_order_relaxed);
                        if (auto* gpuMem = GpuMemoryManager::InstancePtr()) {
                            gpuMem->recordVramAlloc(type, delta);
                        }
                    } else {
                        const uint64_t delta = existing->vramBytes - vramBytes;
                        m_totalVramBytes.fetch_sub(delta, std::memory_order_relaxed);
                        if (auto* gpuMem = GpuMemoryManager::InstancePtr()) {
                            gpuMem->recordVramFree(type, delta);
                        }
                    }
                }
                existing->type = type;
                existing->vramBytes = vramBytes;
                existing->debugName = debugName;
                existing->state = state;
            } else {
                // 登记新记录并分配 Bindless 槽位（严格无锁）
                GpuResourceRecord record;
                record.handle = handle;
                record.type = type;
                record.vramBytes = vramBytes;
                record.debugName = debugName;
                record.state = state;
                record.bindlessSlot = (type == GpuResourceType::Texture) ? acquireBindlessSlot() : UINT32_MAX;

                if (record.bindlessSlot != UINT32_MAX && handle) {
                    if (auto* bindlessMgr = Domain::Renderer::BindlessSet0Manager::InstancePtr()) {
                        bindlessMgr->updateTextureSlot(record.bindlessSlot, static_cast<FCT::Image*>(const_cast<void*>(handle)));
                    }
                }

                m_totalVramBytes.fetch_add(vramBytes, std::memory_order_relaxed);
                if (auto* gpuMem = GpuMemoryManager::InstancePtr()) {
                    gpuMem->recordVramAlloc(type, vramBytes);
                }
                m_table.registerResource(handle, std::move(record));
            }
        }

        void GpuResourceTable::unregisterResource(const void* handle)
        {
            finalizeUnregister(handle);
        }

        void GpuResourceTable::markResourceReleased(const void* handle)
        {
            if (!handle) {
                return;
            }
            if (auto* rec = m_table.findResource(handle)) {
                rec->state = GpuResourceState::Released;
            }
        }

        void GpuResourceTable::finalizeUnregister(const void* handle)
        {
            if (!handle) {
                return;
            }

            auto* rec = m_table.findResource(handle);
            if (!rec) {
                return;
            }

            const uint64_t vram = rec->vramBytes;
            const GpuResourceType rType = rec->type;
            if (vram > 0) {
                m_totalVramBytes.fetch_sub(vram, std::memory_order_relaxed);
                if (auto* gpuMem = GpuMemoryManager::InstancePtr()) {
                    gpuMem->recordVramFree(rType, vram);
                }
            }

            if (rec->bindlessSlot != UINT32_MAX) {
                releaseBindlessSlot(rec->bindlessSlot);
                rec->bindlessSlot = UINT32_MAX;
            }

            m_table.unregisterResource(handle);
        }

        bool GpuResourceTable::contains(const void* handle) const noexcept
        {
            if (!handle) {
                return false;
            }
            return m_table.contains(handle);
        }

        const GpuResourceRecord* GpuResourceTable::getRecord(const void* handle) const noexcept
        {
            if (!handle) {
                return nullptr;
            }
            return m_table.findResource(handle);
        }

        GpuResourceRecord* GpuResourceTable::getRecord(const void* handle) noexcept
        {
            if (!handle) {
                return nullptr;
            }
            return m_table.findResource(handle);
        }

        void GpuResourceTable::updateVram(const void* handle, uint64_t newVramBytes)
        {
            if (!handle) {
                return;
            }
            if (auto* rec = m_table.findResource(handle)) {
                if (newVramBytes != rec->vramBytes) {
                    if (newVramBytes > rec->vramBytes) {
                        const uint64_t delta = newVramBytes - rec->vramBytes;
                        m_totalVramBytes.fetch_add(delta, std::memory_order_relaxed);
                        if (auto* gpuMem = GpuMemoryManager::InstancePtr()) {
                            gpuMem->recordVramAlloc(rec->type, delta);
                        }
                    } else {
                        const uint64_t delta = rec->vramBytes - newVramBytes;
                        m_totalVramBytes.fetch_sub(delta, std::memory_order_relaxed);
                        if (auto* gpuMem = GpuMemoryManager::InstancePtr()) {
                            gpuMem->recordVramFree(rec->type, delta);
                        }
                    }
                    rec->vramBytes = newVramBytes;
                }
            }
        }

        void GpuResourceTable::updateState(const void* handle, GpuResourceState state)
        {
            if (!handle) {
                return;
            }
            if (auto* rec = m_table.findResource(handle)) {
                rec->state = state;
            }
        }

        uint32_t GpuResourceTable::allocateBindlessSlot(const void* handle)
        {
            if (!handle) {
                return UINT32_MAX;
            }
            auto* rec = m_table.findResource(handle);
            if (!rec) {
                return UINT32_MAX;
            }
            if (rec->type != GpuResourceType::Texture) {
                return UINT32_MAX;
            }
            if (rec->bindlessSlot == UINT32_MAX) {
                rec->bindlessSlot = acquireBindlessSlot();
                if (auto* bindlessMgr = Domain::Renderer::BindlessSet0Manager::InstancePtr()) {
                    bindlessMgr->updateTextureSlot(rec->bindlessSlot, static_cast<FCT::Image*>(const_cast<void*>(handle)));
                }
            }
            return rec->bindlessSlot;
        }

        uint32_t GpuResourceTable::getBindlessSlot(const void* handle) const noexcept
        {
            if (!handle) {
                return UINT32_MAX;
            }
            const auto* rec = m_table.findResource(handle);
            if (!rec) {
                return UINT32_MAX;
            }
            return rec->bindlessSlot;
        }

        void GpuResourceTable::setDefaultFallbackHandle(GpuResourceType type, const void* fallbackHandle) noexcept
        {
            const size_t typeIdx = static_cast<size_t>(type);
            if (typeIdx < m_defaultFallbacks.size()) {
                m_defaultFallbacks[typeIdx].store(fallbackHandle, std::memory_order_release);
            }
        }

        const void* GpuResourceTable::getDefaultFallbackHandle(GpuResourceType type) const noexcept
        {
            const size_t typeIdx = static_cast<size_t>(type);
            if (typeIdx < m_defaultFallbacks.size()) {
                return m_defaultFallbacks[typeIdx].load(std::memory_order_acquire);
            }
            return nullptr;
        }

        const void* GpuResourceTable::resolveHandle(const void* handle, GpuResourceType type) const noexcept
        {
            if (handle) {
                const auto* rec = m_table.findResource(handle);
                if (rec && rec->state != GpuResourceState::Released) {
                    return handle;
                }
            }
            // 资源失效或为空时安全回退至默认回退句柄，杜绝硬件层悬空
            return getDefaultFallbackHandle(type);
        }

        size_t GpuResourceTable::activeCount() const noexcept
        {
            return m_table.size();
        }

        uint64_t GpuResourceTable::totalVramBytes() const noexcept
        {
            return m_totalVramBytes.load(std::memory_order_relaxed);
        }

        std::vector<GpuResourceRecord> GpuResourceTable::snapshotRecords() const
        {
            return m_table.snapshot();
        }

        void GpuResourceTable::clear() noexcept
        {
            m_table.clear();
            m_totalVramBytes.store(0, std::memory_order_relaxed);
            m_nextBindlessSlot.store(3, std::memory_order_relaxed);
            uint32_t dummy = 0;
            while (m_freeSlots.pop(dummy)) {}
        }

    } // namespace Resource
} // namespace MQEngine
