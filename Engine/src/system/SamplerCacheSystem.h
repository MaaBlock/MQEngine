//
// Created by MaaBlock on 2025/11/1.
//

#ifndef SAMPLERCACHESYSTEM_H
#define SAMPLERCACHESYSTEM_H
#include "ISystem.h"
namespace MQEngine
{
    struct SamplerDesc {
        FCT::FilterMode magFilter = FCT::FilterMode::Linear;
        FCT::FilterMode minFilter = FCT::FilterMode::Linear;
        FCT::FilterMode mipmapFilter = FCT::FilterMode::Linear;

        FCT::AddressMode addressModeU = FCT::AddressMode::Repeat;
        FCT::AddressMode addressModeV = FCT::AddressMode::Repeat;
        FCT::AddressMode addressModeW = FCT::AddressMode::Repeat;

        float mipLodBias = 0.0f;
        bool anisotropyEnable = false;
        float maxAnisotropy = 1.0f;

        bool compareEnable = false;
        FCT::CompareOp compareOp = FCT::CompareOp::Never;

        float minLod = 0.0f;
        float maxLod = 1000.0f;

        FCT::BorderColor borderColor = FCT::BorderColor::OpaqueBlack;
        bool unnormalizedCoordinates = false;

        bool operator==(const SamplerDesc& other) const {
            return magFilter == other.magFilter &&
                   minFilter == other.minFilter &&
                   mipmapFilter == other.mipmapFilter &&
                   addressModeU == other.addressModeU &&
                   addressModeV == other.addressModeV &&
                   addressModeW == other.addressModeW &&
                   mipLodBias == other.mipLodBias &&
                   anisotropyEnable == other.anisotropyEnable &&
                   maxAnisotropy == other.maxAnisotropy &&
                   compareEnable == other.compareEnable &&
                   compareOp == other.compareOp &&
                   minLod == other.minLod &&
                   maxLod == other.maxLod &&
                   borderColor == other.borderColor &&
                   unnormalizedCoordinates == other.unnormalizedCoordinates;
        }
    };
}

namespace std
{
    template<>
    struct hash<MQEngine::SamplerDesc>
    {
        std::size_t operator()(const MQEngine::SamplerDesc& desc) const noexcept
        {
            std::size_t seed = 0;
            boost::hash_combine(seed, static_cast<int>(desc.magFilter));
            boost::hash_combine(seed, static_cast<int>(desc.minFilter));
            boost::hash_combine(seed, static_cast<int>(desc.mipmapFilter));
            boost::hash_combine(seed, static_cast<int>(desc.addressModeU));
            boost::hash_combine(seed, static_cast<int>(desc.addressModeV));
            boost::hash_combine(seed, static_cast<int>(desc.addressModeW));
            boost::hash_combine(seed, desc.mipLodBias);
            boost::hash_combine(seed, desc.anisotropyEnable);
            boost::hash_combine(seed, desc.maxAnisotropy);
            boost::hash_combine(seed, desc.compareEnable);
            boost::hash_combine(seed, static_cast<int>(desc.compareOp));
            boost::hash_combine(seed, desc.minLod);
            boost::hash_combine(seed, desc.maxLod);
            boost::hash_combine(seed, static_cast<int>(desc.borderColor));
            boost::hash_combine(seed, desc.unnormalizedCoordinates);
            return seed;
        }
    };
}
namespace MQEngine {
    class SamplerCacheSystem : public ISystem {
    public:
        SamplerCacheSystem(Context* ctx);
        void updateLogic() override;
        void updateRender() override;
        Status cacheSampler(const SamplerDesc& desc);
        StatusOr<FCT::Sampler*> getOrCacheSampler(const SamplerDesc& desc);
        StatusOr<FCT::Sampler*> getSampler(size_t hash) const;
    private:
        Context* m_ctx;
        std::unordered_map<size_t, FCT::Sampler*> m_samplerCache;
    };
} // MQEngine

#endif //SAMPLERCACHESYSTEM_H
