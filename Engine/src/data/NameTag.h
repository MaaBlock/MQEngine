#ifndef NAMETAG_H
#define NAMETAG_H
#include "../EnginePCH.h"
#include <string>
#include <boost/serialization/access.hpp>

namespace MQEngine {
    struct ENGINE_API NameTag {
        std::string name;
    private:
        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & name;
        }
    };
} // MQEngine

#endif //NAMETAG_H
