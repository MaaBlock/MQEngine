#include <MQEngine/resource/table/TypeMetaTable.h>

namespace MQEngine
{
    namespace Resource
    {
        const TypeDescriptor* TypeMetaTable::getType(const std::string& name) const noexcept
        {
            auto it = m_descriptorsByName.find(name);
            return (it != m_descriptorsByName.end()) ? &it->second : nullptr;
        }

        const TypeDescriptor* TypeMetaTable::getType(size_t typeHash) const noexcept
        {
            auto it = m_descriptorsByHash.find(typeHash);
            return (it != m_descriptorsByHash.end()) ? &it->second : nullptr;
        }

        bool TypeMetaTable::hasType(const std::string& name) const noexcept
        {
            return m_descriptorsByName.find(name) != m_descriptorsByName.end();
        }

        size_t TypeMetaTable::registeredTypeCount() const noexcept
        {
            return m_descriptorsByName.size();
        }

        void TypeMetaTable::clear() noexcept
        {
            m_descriptorsByName.clear();
            m_descriptorsByHash.clear();
        }

    } // namespace Resource
} // namespace MQEngine
