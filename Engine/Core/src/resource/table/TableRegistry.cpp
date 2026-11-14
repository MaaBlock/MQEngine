#include <MQEngine/resource/table/TableRegistry.h>

namespace MQEngine
{
    namespace Resource
    {
        TableRegistry::TableRegistry() = default;

        TableRegistry::~TableRegistry()
        {
            clear();
        }

        DataBase& TableRegistry::getOrCreateNamedEcsTable(const std::string& tableName)
        {
            auto it = m_namedEcsTables.find(tableName);
            if (it != m_namedEcsTables.end() && it->second) {
                return *it->second;
            }
            auto created = makeUnique<ComponentDataBase>();
            auto* rawPtr = created.get();
            m_namedEcsTables[tableName] = std::move(created);
            return *rawPtr;
        }

        DataBase* TableRegistry::getNamedEcsTable(const std::string& tableName) noexcept
        {
            auto it = m_namedEcsTables.find(tableName);
            return (it != m_namedEcsTables.end()) ? it->second.get() : nullptr;
        }

        void TableRegistry::setKeyValue(const std::string& key, const std::string& value)
        {
            // 严格无锁更新/插入键值节点
            KvNode* curr = m_kvHead.load(std::memory_order_acquire);
            while (curr) {
                if (curr->key == key) {
                    curr->value = value;
                    return;
                }
                curr = curr->next;
            }

            auto* newNode = new KvNode{key, value, nullptr};
            newNode->next = m_kvHead.load(std::memory_order_relaxed);
            while (!m_kvHead.compare_exchange_weak(newNode->next, newNode, std::memory_order_release, std::memory_order_relaxed)) {
                // CAS 重试
            }
        }

        std::string TableRegistry::getKeyValue(const std::string& key, const std::string& defaultValue) const
        {
            const KvNode* curr = m_kvHead.load(std::memory_order_acquire);
            while (curr) {
                if (curr->key == key) {
                    return curr->value;
                }
                curr = curr->next;
            }
            return defaultValue;
        }

        bool TableRegistry::hasKey(const std::string& key) const noexcept
        {
            const KvNode* curr = m_kvHead.load(std::memory_order_acquire);
            while (curr) {
                if (curr->key == key) {
                    return true;
                }
                curr = curr->next;
            }
            return false;
        }

        void TableRegistry::clear() noexcept
        {
            m_primaryDataBase.clear();
            m_namedEcsTables.clear();
            m_textureTable.clear();
            m_meshTable.clear();
            m_materialTable.clear();
            m_pipelineTable.clear();
            m_shaderTable.clear();
            m_gpuResourceTable.clear();
            m_typeMetaTable.clear();
            m_schemaTable.clear();

            KvNode* head = m_kvHead.exchange(nullptr, std::memory_order_acq_rel);
            while (head) {
                KvNode* next = head->next;
                delete head;
                head = next;
            }
        }

    } // namespace Resource
} // namespace MQEngine
