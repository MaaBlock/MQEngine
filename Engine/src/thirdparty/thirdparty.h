/**
 * @file ThirdParty.h
 * @brief A header file containing third party libraries and macros for platform
 *
 */
#ifndef ENGINE_THIRDPARTY_H
#define ENGINE_THIRDPARTY_H
#include <FCT_Node.h>
#include <FCT.h>
#include <FCT_IMGUI.h>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <string>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <spdlog/spdlog.h>
namespace MQEngine
{
    using Context = FCT::Context;
    using Window = FCT::Window;
    using Uniform = FCT::Uniform;
    using ShaderRef = FCT::ShaderRef;
    //using EventPipe = FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly>;
}
#endif //THIRDPARTY_H
