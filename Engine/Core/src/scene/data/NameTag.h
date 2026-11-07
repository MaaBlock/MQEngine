#ifndef NAMETAG_H
#define NAMETAG_H
#include "../../EnginePCH.h"
#include <string>
#include <boost/serialization/access.hpp>
#include <boost/serialization/version.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace MQEngine {
    struct ENGINE_API NameTag {
        std::string name;
        std::string uuid = boost::uuids::to_string(boost::uuids::random_generator()());

    private:
        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & name;
            if (version > 0) {
                ar & uuid;
            }
        }
    };
} // MQEngine

BOOST_CLASS_VERSION(MQEngine::NameTag, 1)

#endif //NAMETAG_H
