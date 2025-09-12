//
// Created by Administrator on 2025/8/24.
//

#ifndef CAMERA_H
#define CAMERA_H
#include "../EnginePCH.h"
#include "../Thirdparty/thirdparty.h"
#include <boost/describe.hpp>
namespace MQEngine {
    constexpr FCT::UniformSlot CameraUniformSlot {
        "CameraUniform",
        FCT::UniformVar{FCT::UniformType::ProjectionMatrix},
        FCT::UniformVar{FCT::UniformType::ViewMatrix}
    };
    struct ENGINE_API PositionComponent {
        FCT::Vec3 position;
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & position;
        }
    };
    BOOST_DESCRIBE_STRUCT(PositionComponent, (), (position))

    struct ENGINE_API RotationComponent {
        FCT::Vec3 rotation;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & rotation;
        }
    };
    BOOST_DESCRIBE_STRUCT(RotationComponent, (), (rotation))

    struct ENGINE_API ScaleComponent {
        FCT::Vec3 scale = FCT::Vec3(1.0f, 1.0f, 1.0f);
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & scale;
        }
    };
    BOOST_DESCRIBE_STRUCT(ScaleComponent, (), (scale))

    struct ENGINE_API CameraComponent {
        bool active = true;
        float fov = 45.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & fov;
            ar & nearPlane;
            ar & farPlane;
        }
    };
    BOOST_DESCRIBE_STRUCT(CameraComponent, (), (active, fov, nearPlane, farPlane))

    struct ENGINE_API CacheRotationMatrix {
        FCT::Mat4 rotationMatrix;
    };

    constexpr FCT::UniformSlot ModelUniformSlot {
        "ModelUniform",
        FCT::UniformVar{FCT::UniformType::ModelMatrix,"modelMatrix"}
    };

    struct ENGINE_API CacheModelMatrix {
        FCT::Uniform* uniform = nullptr;
        bool init = false;
        bool ownsUniform = true;  // 标记是否拥有uniform的所有权
        
        CacheModelMatrix() = delete;
        explicit CacheModelMatrix(FCT::Context* ctx) :  init(false), ownsUniform(true)
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
            : uniform(other.uniform), init(other.init), ownsUniform(other.ownsUniform) {
            other.uniform = nullptr;
            other.ownsUniform = false;
        }
        
        CacheModelMatrix& operator=(CacheModelMatrix&& other) noexcept {
            if (this != &other) {
                if (ownsUniform && uniform) {
                    delete uniform;
                }
                uniform = other.uniform;
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
