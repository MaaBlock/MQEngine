#ifndef MQENINE_ENTT_ARCHIVE_WRAPPER_H
#define MQENINE_ENTT_ARCHIVE_WRAPPER_H
#include "../../thirdparty/thirdparty.h"
#include "NameTag.h"

namespace MQEngine {

    class EnttOutputArchiveWrapper {
    private:
        boost::archive::binary_oarchive& m_archive;

        template<typename FieldT>
        void writeField(const FieldT& field) {
            if constexpr (!FCT::NB::is_pfr_reflectable_v<FieldT> ||
                          requires(FieldT& mutableField, boost::archive::binary_oarchive& ar) { mutableField.serialize(ar, 0u); }) {
                m_archive << field;
            } else {
                boost::pfr::for_each_field(field, [this](const auto& subField) {
                    writeField(subField);
                });
            }
        }

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
            writeField(component);
        }
    };

    class EnttInputArchiveWrapper {
    private:
        boost::archive::binary_iarchive& m_archive;
        bool m_hasEntityCount = false;
        std::underlying_type_t<entt::entity> m_maxEntityCount = 0;

        template<typename FieldT>
        void readField(FieldT& field) {
            if constexpr (!FCT::NB::is_pfr_reflectable_v<FieldT> ||
                          requires(FieldT& mutableField, boost::archive::binary_iarchive& ar) { mutableField.serialize(ar, 0u); }) {
                m_archive >> field;
            } else {
                boost::pfr::for_each_field(field, [this](auto& subField) {
                    readField(subField);
                });
            }
        }

    public:
        explicit EnttInputArchiveWrapper(boost::archive::binary_iarchive& archive)
            : m_archive(archive) {}

        void operator()(std::underlying_type_t<entt::entity>& size) {
            try {
                m_archive >> size;
            } catch (const boost::archive::archive_exception&) {
                size = 0;
                return;
            }
            if (!m_hasEntityCount) {
                m_hasEntityCount = true;
                if (size > 65536u) {
                    throw std::runtime_error("EnttInputArchiveWrapper: 非法的实体总数");
                }
                m_maxEntityCount = size;
            } else if (size > m_maxEntityCount) {
                throw std::runtime_error("EnttInputArchiveWrapper: 组件池数量超出实体总数，触发扩展段恢复");
            }
        }

        void operator()(entt::entity& entity) {
            std::underlying_type_t<entt::entity> underlying_value;
            m_archive >> underlying_value;
            entity = static_cast<entt::entity>(underlying_value);
        }

        template<typename T>
        void operator()(T& component) {
            readField(component);
        }
    };
}
#endif //
