#ifndef ENGINGE_SINGLEQUEUEEVENTSYSTEM_H
#define ENGINGE_SINGLEQUEUEEVENTSYSTEM_H
#include "../Thirdparty/thirdparty.h"
#include <memory>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <vector>
#include <boost/lockfree/queue.hpp>

namespace MQEngine
{
    using SubscribeId = std::size_t;

    /**
     * @brief 单队列事件系统 - 完全无锁的线程安全实现
     *
     * 使用模型：
     * - 多个生产者线程可以同时调用 enqueue() 和 subscribe()
     * - 单个消费者线程调用 processNext()/processAll()
     */
    class SingleQueueEventSystem {
    private:
        template<typename Event>
        static constexpr auto getEventTypeId() {
            return entt::type_hash<Event>::value();
        }

        // 事件处理器接口
        struct IEventHandler {
            virtual ~IEventHandler() = default;
            virtual Status handle(const void* event) = 0;
        };

        template<typename Event>
        struct EventHandler : IEventHandler {
            std::function<Status(const Event&)> handler;

            EventHandler(std::function<Status(const Event&)> h) : handler(std::move(h)) {}

            Status handle(const void* event) override {
                return handler(*static_cast<const Event*>(event));
            }
        };

        // 队列中的事件包装
        struct QueuedEvent {
            entt::id_type type_id;
            std::shared_ptr<void> data;

            template<typename Event>
            QueuedEvent(Event&& event)
                : type_id(entt::type_hash<std::decay_t<Event>>::value())
                , data(std::make_shared<std::decay_t<Event>>(std::forward<Event>(event))) {}

            QueuedEvent() : type_id(0), data(nullptr) {}
        };

        // 订阅操作（用于无锁订阅管理）
        struct SubscribeOp {
            enum class Type { Add, Remove, RemoveAll, RemoveAny };
            Type op_type;
            entt::id_type event_type_id;
            SubscribeId subscribe_id;
            std::unique_ptr<IEventHandler> handler;

            SubscribeOp() : op_type(Type::Add), event_type_id(0), subscribe_id(0) {}
        };

        // 订阅者管理（仅在消费者线程中访问，无需锁）
        std::unordered_map<entt::id_type, std::unordered_map<SubscribeId, std::unique_ptr<IEventHandler>>> m_handlers;

        // 无锁事件队列
        boost::lockfree::queue<QueuedEvent*> m_eventQueue;

        // 无锁订阅操作队列
        boost::lockfree::queue<SubscribeOp*> m_subscribeQueue;

        // 订阅 ID 计数器（原子操作）
        std::atomic<SubscribeId> m_subscribeIdCounter{1};

        // 内部触发函数（仅在消费者线程中调用）
        // 返回所有处理器的状态，如果有任何失败则返回第一个失败状态
        Status triggerInternal(entt::id_type type_id, const void* event) {
            auto it = m_handlers.find(type_id);
            if (it != m_handlers.end()) {
                for (const auto& [id, handler] : it->second) {
                    Status status = handler->handle(event);
                    if (!status.ok()) {
                        return status;
                    }
                }
            }
            return absl::OkStatus();
        }

        // 处理订阅操作（仅在消费者线程中调用）
        void processSubscribeOps() {
            SubscribeOp* op = nullptr;
            while (m_subscribeQueue.pop(op)) {
                switch (op->op_type) {
                    case SubscribeOp::Type::Add:
                        m_handlers[op->event_type_id][op->subscribe_id] = std::move(op->handler);
                        break;

                    case SubscribeOp::Type::Remove: {
                        auto it = m_handlers.find(op->event_type_id);
                        if (it != m_handlers.end()) {
                            it->second.erase(op->subscribe_id);
                            if (it->second.empty()) {
                                m_handlers.erase(it);
                            }
                        }
                        break;
                    }

                    case SubscribeOp::Type::RemoveAll:
                        m_handlers.erase(op->event_type_id);
                        break;

                    case SubscribeOp::Type::RemoveAny: {
                        // 遍历所有事件类型查找并删除
                        for (auto& [typeId, handlers] : m_handlers) {
                            auto it = handlers.find(op->subscribe_id);
                            if (it != handlers.end()) {
                                handlers.erase(it);
                                if (handlers.empty()) {
                                    m_handlers.erase(typeId);
                                }
                                break;
                            }
                        }
                        break;
                    }
                }
                delete op;
            }
        }

    public:
        /**
         * @brief 构造函数
         * @param eventCapacity 事件队列容量
         * @param subscribeCapacity 订阅操作队列容量
         */
        explicit SingleQueueEventSystem(size_t eventCapacity = 4096, size_t subscribeCapacity = 256)
            : m_eventQueue(eventCapacity)
            , m_subscribeQueue(subscribeCapacity) {}

        ~SingleQueueEventSystem() {
            // 清理事件队列
            QueuedEvent* event;
            while (m_eventQueue.pop(event)) {
                delete event;
            }

            // 清理订阅操作队列
            SubscribeOp* op;
            while (m_subscribeQueue.pop(op)) {
                delete op;
            }
        }

        // 禁止拷贝和移动
        SingleQueueEventSystem(const SingleQueueEventSystem&) = delete;
        SingleQueueEventSystem& operator=(const SingleQueueEventSystem&) = delete;
        SingleQueueEventSystem(SingleQueueEventSystem&&) = delete;
        SingleQueueEventSystem& operator=(SingleQueueEventSystem&&) = delete;

        /**
         * @brief 将事件加入队列（完全无锁，多线程安全）
         * @return 成功返回 true，队列满返回 false
         */
        template<typename Event>
        bool enqueue(Event&& event) {
            auto* queuedEvent = new QueuedEvent(std::forward<Event>(event));
            if (!m_eventQueue.push(queuedEvent)) {
                // 队列满了
                delete queuedEvent;
                return false;
            }
            return true;
        }

        /**
         * @brief 将事件加入队列，如果队列满则自旋等待（完全无锁，多线程安全）
         */
        template<typename Event>
        void enqueueWait(Event&& event) {
            auto* queuedEvent = new QueuedEvent(std::forward<Event>(event));
            while (!m_eventQueue.push(queuedEvent)) {
                std::this_thread::yield();
            }
        }

        /**
         * @brief 处理队列中的下一个事件（应在单一消费者线程中调用）
         * @return 事件处理的状态，如果队列为空返回 OkStatus
         */
        Status processNext() {
            // 先处理订阅操作
            processSubscribeOps();

            QueuedEvent* event = nullptr;
            if (m_eventQueue.pop(event)) {
                Status status = triggerInternal(event->type_id, event->data.get());
                delete event;
                return status;
            }
            return absl::OkStatus();
        }

        /**
         * @brief 处理队列中的所有事件
         * @return 处理结果的状态，如果有任何事件处理失败则返回第一个失败状态
         */
        Status processAll() {
            // 先处理订阅操作
            processSubscribeOps();

            // 然后处理所有事件
            QueuedEvent* event = nullptr;
            while (m_eventQueue.pop(event)) {
                Status status = triggerInternal(event->type_id, event->data.get());
                delete event;
                if (!status.ok()) {
                    return status;
                }
            }

            return absl::OkStatus();
        }

        /**
         * @brief 处理指定数量的事件
         * @param maxCount 最多处理的事件数量
         * @return 处理结果的状态，如果有任何事件处理失败则返回第一个失败状态
         */
        Status processN(size_t maxCount) {
            // 先处理订阅操作
            processSubscribeOps();

            // 然后处理指定数量的事件
            size_t count = 0;
            QueuedEvent* event = nullptr;
            while (count < maxCount && m_eventQueue.pop(event)) {
                Status status = triggerInternal(event->type_id, event->data.get());
                delete event;
                ++count;
                if (!status.ok()) {
                    return status;
                }
            }

            return absl::OkStatus();
        }

        /**
         * @brief 订阅事件（完全无锁，多线程安全）
         * 注意：订阅操作会在下次 processNext/processAll/processN 时生效
         * @return 订阅 ID，用于后续取消订阅
         */
        template<typename Event, typename Func>
        SubscribeId subscribe(Func&& func) {
            constexpr auto eventTypeId = getEventTypeId<Event>();

            SubscribeId subscribeId = m_subscribeIdCounter.fetch_add(1, std::memory_order_relaxed);

            auto* op = new SubscribeOp();
            op->op_type = SubscribeOp::Type::Add;
            op->event_type_id = eventTypeId;
            op->subscribe_id = subscribeId;
            op->handler = std::make_unique<EventHandler<Event>>(
                std::function<Status(const Event&)>(std::forward<Func>(func))
            );

            while (!m_subscribeQueue.push(op)) {
                std::this_thread::yield();
            }

            return subscribeId;
        }

        /**
         * @brief 取消订阅指定事件类型的特定订阅（完全无锁，多线程安全）
         * 注意：取消订阅操作会在下次 processNext/processAll/processN 时生效
         */
        template<typename Event>
        bool unsubscribe(SubscribeId subscribeId) {
            constexpr auto eventTypeId = getEventTypeId<Event>();

            auto* op = new SubscribeOp();
            op->op_type = SubscribeOp::Type::Remove;
            op->event_type_id = eventTypeId;
            op->subscribe_id = subscribeId;

            if (!m_subscribeQueue.push(op)) {
                delete op;
                return false;
            }
            return true;
        }

        /**
         * @brief 取消订阅（自旋等待版本）
         */
        template<typename Event>
        void unsubscribeWait(SubscribeId subscribeId) {
            constexpr auto eventTypeId = getEventTypeId<Event>();

            auto* op = new SubscribeOp();
            op->op_type = SubscribeOp::Type::Remove;
            op->event_type_id = eventTypeId;
            op->subscribe_id = subscribeId;

            while (!m_subscribeQueue.push(op)) {
                std::this_thread::yield();
            }
        }

        /**
         * @brief 取消所有订阅（自旋等待版本）
         */
        template<typename Event>
        void unsubscribeAllWait() {
            constexpr auto eventTypeId = getEventTypeId<Event>();

            auto* op = new SubscribeOp();
            op->op_type = SubscribeOp::Type::RemoveAll;
            op->event_type_id = eventTypeId;
            op->subscribe_id = 0;

            while (!m_subscribeQueue.push(op)) {
                std::this_thread::yield();
            }
        }

        /**
         * @brief 取消任意类型订阅（自旋等待版本）
         */
        void unsubscribeWait(SubscribeId subscribeId) {
            auto* op = new SubscribeOp();
            op->op_type = SubscribeOp::Type::RemoveAny;
            op->event_type_id = 0;
            op->subscribe_id = subscribeId;

            while (!m_subscribeQueue.push(op)) {
                std::this_thread::yield();
            }
        }

        /**
         * @brief 检查事件队列是否为空（无锁）
         */
        bool empty() const {
            return m_eventQueue.empty();
        }

        /**
         * @brief 清空事件队列（应在消费者线程中调用）
         * 注意：这会丢弃所有未处理的事件
         */
        void clear() {
            QueuedEvent* event;
            while (m_eventQueue.pop(event)) {
                delete event;
            }
        }

        /**
         * @brief 检查队列是否无锁
         */
        bool is_lock_free() const {
            return m_eventQueue.is_lock_free() && m_subscribeQueue.is_lock_free();
        }
    };

} // namespace MQEngine

#endif //SINGLEQUEUEEVENTSYSTEM_H