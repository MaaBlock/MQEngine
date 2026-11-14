#ifndef MQENGINE_FCTIMPORTER_H
#define MQENGINE_FCTIMPORTER_H
namespace MQEngine
{
    using FCT::SetThreadName;
    using FCT::Local;
    using FCT::Locale;
    using FCT::SetLocal;
    using FCT::SetLocale;
    using FCT::GetLocal;
    using FCT::GetLocale;
    using FCT::SetupUtf8;
    using FCT::Context;
    using FCT::Window;
    using FCT::Uniform;
    using FCT::ShaderRef;
    using FCT::UniquePtr;
    using FCT::makeUnique;
    using FCT::Ptr;
    using FCT::ModelLoader;
    using FCT::Sampler;
    using PlatformRuntime = FCT::Runtime;
    using FCT::Vec2;
    using FCT::Vec3;
    using FCT::Vec3f;
    using FCT::Vec3d;
    using FCT::Vec4;
    using FCT::Quat;
    using FCT::Mat4;
    using FCT::Image;
    using FCT::RenderGraph;
    using FCT::ResourceGroup;
    using FCT::ResourceRequireDesc;
    using FCT::TokenGraph;
    using FCT::MemoryTag;
    using FCT::MemoryStatsSnapshot;
    using FCT::MemoryTracker;
    using FCT::LinearArena;
    using FCT::DoubleBufferedFrameArena;
    using FCT::BlockMemoryPool;
    using FCT::ScopedPoolBuffer;
    using FCT::JobPriority;
    using FCT::JobCounter;
    using FCT::ThreadPool;
    using FCT::Computation;
    using FCT::ComputationSharedState;
    using FCT::SAIPath;
    using FCT::SAIFileEntry;
    using FCT::SAIFileSystem;
    using FCT::SAISystem;
    using FCT::SAIPathSearcher;
    namespace NB = FCT::NB;
#ifndef __ANDROID__
    namespace PyBri = FCT::PyBri;
#endif
    namespace SAI = FCT::SAI;
}
#endif // MQENGINE_FCTIMPORTER_H
