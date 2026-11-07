//
// Created by Administrator on 2025/8/24.
//

#ifndef CAMERA_H
#define CAMERA_H
#include "../../EnginePCH.h"
#include "../../thirdparty/thirdparty.h"
namespace MQEngine {
    constexpr FCT::UniformSlot CameraUniformSlot {
        "CameraUniform",
        FCT::UniformVar{FCT::UniformType::ProjectionMatrix},
        FCT::UniformVar{FCT::UniformType::ViewMatrix}
    };
    struct ENGINE_API PositionComponent {
        FCT::Vec3<> position;
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & position;
        }
    };

    struct ENGINE_API RotationComponent {
        FCT::Vec3<> rotation;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & rotation;
        }
    };

    struct ENGINE_API ScaleComponent {
        FCT::Vec3<> scale = FCT::Vec3(1.0f, 1.0f, 1.0f);
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & scale;
        }
    };

    struct ENGINE_API CameraComponent {
        bool active = true;
        float fov = 45.0f;
        float nearPlane = 0.5f;
        float farPlane = 20000000.0f;
        bool reverseZ = true;
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & fov;
            ar & nearPlane;
            ar & farPlane;
            if (version > 0) {
                ar & active;
                ar & reverseZ;
            }
        }
    };

    struct ENGINE_API HierarchyComponent {
        std::string parentUuid;
        uint32_t parentEntity = static_cast<uint32_t>(entt::null);
        std::vector<std::string> childrenUuids;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & parentUuid;
            ar & parentEntity;
            ar & childrenUuids;
        }
    };

    struct ENGINE_API RuntimeSubEntityTag {
        uint32_t ownerEntity = static_cast<uint32_t>(entt::null);
        bool hideInHierarchy = true;
    };

    struct ENGINE_API CacheRotationMatrix {
        FCT::Mat4 rotationMatrix;
        FCT::Mat4 worldRotationMatrix;
    };

    constexpr FCT::UniformSlot ModelUniformSlot {
        "ModelUniform",
        FCT::UniformVar{FCT::UniformType::ModelMatrix,"modelMatrix"},
        FCT::UniformVar{FCT::UniformType::Mat4,"modelInverseTransposeMatrix"}
    };

    struct ENGINE_API CacheModelMatrix {
        FCT::Uniform* uniform = nullptr;
        FCT::Mat4 localMatrix;
        FCT::Mat4 worldMatrix;
        bool init = false;
        bool ownsUniform = true;  // 标记是否拥有uniform的所有权
        
        CacheModelMatrix()
        {

        }
        explicit CacheModelMatrix(FCT::Context* ctx) : init(false), ownsUniform(true)
        {
            uniform = new FCT::Uniform(ctx, ModelUniformSlot);
        }
        
        ~CacheModelMatrix() {
            if (ownsUniform && uniform) {
                delete uniform;
                uniform = nullptr;
            }
        }
        
        CacheModelMatrix(const CacheModelMatrix&) = delete;
        CacheModelMatrix& operator=(const CacheModelMatrix&) = delete;
        
        CacheModelMatrix(CacheModelMatrix&& other) noexcept 
            : uniform(other.uniform), localMatrix(other.localMatrix), worldMatrix(other.worldMatrix), init(other.init), ownsUniform(other.ownsUniform) {
            other.uniform = nullptr;
            other.ownsUniform = false;
        }
        
        CacheModelMatrix& operator=(CacheModelMatrix&& other) noexcept {
            if (this != &other) {
                if (ownsUniform && uniform) {
                    delete uniform;
                }
                uniform = other.uniform;
                localMatrix = other.localMatrix;
                worldMatrix = other.worldMatrix;
                init = other.init;
                ownsUniform = other.ownsUniform;
                other.uniform = nullptr;
                other.ownsUniform = false;
            }
            return *this;
        }
    };

} // MQEngine

#endif //CAMERA_H
