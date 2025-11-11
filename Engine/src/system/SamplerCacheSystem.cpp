//
// Created by MaaBlock on 2025/11/1.
//

#include "SamplerCacheSystem.h"

namespace MQEngine {
    SamplerCacheSystem::SamplerCacheSystem(Context* ctx)
    {
        m_ctx = ctx;
    }
    void SamplerCacheSystem::updateLogic()
    {

    }
    void SamplerCacheSystem::updateRender()
    {

    }
    Status SamplerCacheSystem::cacheSampler(const SamplerDesc& desc)
    {
        size_t hash = std::hash<SamplerDesc>{}(desc);
        auto it = m_samplerCache.find(hash);
        if (it!= m_samplerCache.end())
            return OkStatus();
        FCT::Sampler* sampler = m_ctx->createResource<FCT::Sampler>();
        if (!sampler)
            return InternalError("创建Sampler资源失败");
        sampler->setFilter(desc.magFilter, desc.minFilter, desc.mipmapFilter);
        sampler->setAddressMode(desc.addressModeU, desc.addressModeV, desc.addressModeW);
        sampler->setAnisotropy(desc.anisotropyEnable, desc.maxAnisotropy);
        sampler->setCompare(desc.compareEnable, desc.compareOp);
        sampler->setLodRange(desc.minLod, desc.maxLod, desc.mipLodBias);
        sampler->setBorderColor(desc.borderColor);
        sampler->setUnnormalizedCoordinates(desc.unnormalizedCoordinates);

        sampler->create();

        m_samplerCache[hash] = sampler;

        return OkStatus();
    }
    StatusOr<FCT::Sampler*> SamplerCacheSystem::getOrCacheSampler(const SamplerDesc& desc)
    {
        size_t hash = std::hash<SamplerDesc>{}(desc);
        auto it = m_samplerCache.find(hash);
        if (it!= m_samplerCache.end())
            return it->second;
        return cacheSampler(desc);
    }
    StatusOr<FCT::Sampler*> SamplerCacheSystem::getSampler(size_t hash) const {
        auto it = m_samplerCache.find(hash);
        if (it!= m_samplerCache.end())
            return it->second;
        return NotFoundError("未在缓存中找到Sampler");
    }
} // MQEngine