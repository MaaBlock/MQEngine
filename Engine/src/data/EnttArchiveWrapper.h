
#ifndef MQENINE_ENTT_ARCHIVE_WRAPPER_H
#define MQENINE_ENTT_ARCHIVE_WRAPPER_H
#include "../thirdparty/thirdparty.h"

namespace MQEngine {

    class EnttOutputArchiveWrapper {
    private:
        boost::archive::binary_oarchive& m_archive;

    public:
        explicit EnttOutputArchiveWrapper(boost::archive::binary_oarchive& archive)
            : m_archive(archive) {}

        void operator()(std::underlying_type_t<entt::entity> size) {
            m_archive << size;
        }

        void operator()(entt::entity entity) {
            std::underlying_type_t<entt::entity> underlying_value = static_cast<std::underlying_type_t<entt::entity>>(entity);
            m_archive << underlying_value;
        }

        template<typename T>
        void operator()(const T& component) {
            m_archive << component;
        }
    };

    class EnttInputArchiveWrapper {
    private:
        boost::archive::binary_iarchive& m_archive;

    public:
        explicit EnttInputArchiveWrapper(boost::archive::binary_iarchive& archive)
            : m_archive(archive) {}

        void operator()(std::underlying_type_t<entt::entity>& size) {
            m_archive >> size;
        }

        void operator()(entt::entity& entity) {
            std::underlying_type_t<entt::entity> underlying_value;
            m_archive >> underlying_value;
            entity = static_cast<entt::entity>(underlying_value);
        }

        template<typename T>
        void operator()(T& component) {
            m_archive >> component;
        }
    };
}
#endif //
