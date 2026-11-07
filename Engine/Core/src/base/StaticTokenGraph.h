#ifndef MQENGINE_BASE_STATIC_TOKEN_GRAPH_H
#define MQENGINE_BASE_STATIC_TOKEN_GRAPH_H

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace MQEngine
{
    /**
     * @brief 编译期类型列表容器
     */
    template <typename... Ts>
    struct TypeList
    {
        static constexpr size_t size = sizeof...(Ts);
    };

    /**
     * @brief 编译期静态图节点依赖特征（可通过特化或在节点类内定义 Predecessors / Successors TypeList）
     */
    template <typename T, typename = void>
    struct StaticGraphPredecessors
    {
        using type = TypeList<>;
    };

    template <typename T>
    struct StaticGraphPredecessors<T, std::void_t<typename T::Predecessors>>
    {
        using type = typename T::Predecessors;
    };

    template <typename T, typename = void>
    struct StaticGraphSuccessors
    {
        using type = TypeList<>;
    };

    template <typename T>
    struct StaticGraphSuccessors<T, std::void_t<typename T::Successors>>
    {
        using type = typename T::Successors;
    };

    /**
     * @brief 显式包装静态图节点及其编译期前驱/后继依赖（无需侵入式修改节点类本身）
     */
    template <typename NodeType,
              typename PreList = typename StaticGraphPredecessors<NodeType>::type,
              typename SuccList = typename StaticGraphSuccessors<NodeType>::type>
    struct StaticTokenNode
    {
        using Type = NodeType;
        using Predecessors = PreList;
        using Successors = SuccList;
    };

    namespace Detail
    {
        template <typename T>
        struct UnwrapStaticNode
        {
            using Type = T;
            using Predecessors = typename StaticGraphPredecessors<T>::type;
            using Successors = typename StaticGraphSuccessors<T>::type;
        };

        template <typename NodeType, typename PreList, typename SuccList>
        struct UnwrapStaticNode<StaticTokenNode<NodeType, PreList, SuccList>>
        {
            using Type = NodeType;
            using Predecessors = PreList;
            using Successors = SuccList;
        };

        template <typename Target, typename... Types>
        constexpr size_t findTypeIndex()
        {
            constexpr bool matches[] = {std::is_same_v<Target, Types>...};
            for (size_t i = 0; i < sizeof...(Types); ++i)
            {
                if (matches[i])
                {
                    return i;
                }
            }
            return sizeof...(Types);
        }

        template <size_t N>
        struct StaticTopoSortResult
        {
            std::array<size_t, N> order{};
            std::array<size_t, N> reverseOrder{};
            bool isAcyclic = true;
        };

        template <typename... RawNodes, typename... PreTypes>
        constexpr void addPredecessorEdges(std::array<std::array<bool, sizeof...(RawNodes)>, sizeof...(RawNodes)>& adj,
                                           size_t currentIdx,
                                           TypeList<PreTypes...>)
        {
            constexpr size_t N = sizeof...(RawNodes);
            (
                [&]() {
                    constexpr size_t preIdx = findTypeIndex<PreTypes, RawNodes...>();
                    static_assert(preIdx < N, "StaticTokenGraph: Predecessors 中包含未在图中注册的节点类型");
                    adj[preIdx][currentIdx] = true;
                }(),
                ...);
        }

        template <typename... RawNodes, typename... SuccTypes>
        constexpr void addSuccessorEdges(std::array<std::array<bool, sizeof...(RawNodes)>, sizeof...(RawNodes)>& adj,
                                         size_t currentIdx,
                                         TypeList<SuccTypes...>)
        {
            constexpr size_t N = sizeof...(RawNodes);
            (
                [&]() {
                    constexpr size_t succIdx = findTypeIndex<SuccTypes, RawNodes...>();
                    static_assert(succIdx < N, "StaticTokenGraph: Successors 中包含未在图中注册的节点类型");
                    adj[currentIdx][succIdx] = true;
                }(),
                ...);
        }

        template <typename... Nodes>
        constexpr StaticTopoSortResult<sizeof...(Nodes)> computeStaticTopoSort()
        {
            constexpr size_t N = sizeof...(Nodes);
            StaticTopoSortResult<N> result{};
            if constexpr (N == 0)
            {
                return result;
            }
            else
            {
                std::array<std::array<bool, N>, N> adj{};
                size_t nodeIdx = 0;
                (
                    [&]() {
                        using Unwrapped = UnwrapStaticNode<Nodes>;
                        addPredecessorEdges<typename UnwrapStaticNode<Nodes>::Type...>(
                            adj, nodeIdx, typename Unwrapped::Predecessors{});
                        addSuccessorEdges<typename UnwrapStaticNode<Nodes>::Type...>(
                            adj, nodeIdx, typename Unwrapped::Successors{});
                        ++nodeIdx;
                    }(),
                    ...);

                std::array<size_t, N> inDegree{};
                for (size_t u = 0; u < N; ++u)
                {
                    for (size_t v = 0; v < N; ++v)
                    {
                        if (adj[u][v])
                        {
                            ++inDegree[v];
                        }
                    }
                }

                std::array<bool, N> visited{};
                size_t sortedCount = 0;

                // Kahn 编译期拓扑排序（同入度为 0 时保持模板声明相对顺序稳定）
                for (size_t step = 0; step < N; ++step)
                {
                    size_t picked = N;
                    for (size_t i = 0; i < N; ++i)
                    {
                        if (!visited[i] && inDegree[i] == 0)
                        {
                            picked = i;
                            break;
                        }
                    }
                    if (picked == N)
                    {
                        result.isAcyclic = false;
                        return result;
                    }

                    visited[picked] = true;
                    result.order[sortedCount++] = picked;

                    for (size_t v = 0; v < N; ++v)
                    {
                        if (adj[picked][v] && inDegree[v] > 0)
                        {
                            --inDegree[v];
                        }
                    }
                }

                for (size_t i = 0; i < N; ++i)
                {
                    result.reverseOrder[i] = result.order[N - 1 - i];
                }
                result.isAcyclic = (sortedCount == N);
                return result;
            }
        }
    } // namespace Detail

    /**
     * @brief C++20 编译期静态 Token 拓扑图：
     *        在编译期（consteval）根据各节点的 Predecessors / Successors 完成拓扑排序与环检测，
     *        运行期通过变参折叠表达式直接展开为零开销的顺序内联调用。
     */
    template <typename... Nodes>
    class StaticTokenGraph
    {
    public:
        static constexpr size_t kNodeCount = sizeof...(Nodes);
        using NodeTuple = std::tuple<typename Detail::UnwrapStaticNode<Nodes>::Type...>;

        static constexpr Detail::StaticTopoSortResult<kNodeCount> kSortResult =
            Detail::computeStaticTopoSort<Nodes...>();

        static_assert(kSortResult.isAcyclic, "StaticTokenGraph 编译期拓扑排序失败：存在循环依赖 (Cycle Detected)");

        static constexpr const std::array<size_t, kNodeCount>& sortedIndices() noexcept
        {
            return kSortResult.order;
        }

        static constexpr const std::array<size_t, kNodeCount>& reverseSortedIndices() noexcept
        {
            return kSortResult.reverseOrder;
        }

        template <size_t OrderPos>
        using SortedNodeType = std::tuple_element_t<kSortResult.order[OrderPos], NodeTuple>;

        template <size_t OrderPos>
        using ReverseSortedNodeType = std::tuple_element_t<kSortResult.reverseOrder[OrderPos], NodeTuple>;

        /**
         * @brief 按编译期拓扑正序零开销展开调用 `fn(std::type_identity<NodeType>{})`
         */
        template <typename Fn>
        static constexpr void forEachForward(Fn&& fn)
        {
            forEachForwardImpl(std::forward<Fn>(fn), std::make_index_sequence<kNodeCount>{});
        }

        /**
         * @brief 按编译期拓扑逆序零开销展开调用 `fn(std::type_identity<NodeType>{})`
         */
        template <typename Fn>
        static constexpr void forEachReverse(Fn&& fn)
        {
            forEachReverseImpl(std::forward<Fn>(fn), std::make_index_sequence<kNodeCount>{});
        }

    private:
        template <typename NodeT, typename Fn>
        static constexpr void invokeForNode(Fn&& fn)
        {
            if constexpr (requires { fn(std::type_identity<NodeT>{}); })
            {
                fn(std::type_identity<NodeT>{});
            }
            else
            {
                fn.template operator()<NodeT>();
            }
        }

        template <typename Fn, size_t... Is>
        static constexpr void forEachForwardImpl(Fn&& fn, std::index_sequence<Is...>)
        {
            (invokeForNode<SortedNodeType<Is>>(fn), ...);
        }

        template <typename Fn, size_t... Is>
        static constexpr void forEachReverseImpl(Fn&& fn, std::index_sequence<Is...>)
        {
            (invokeForNode<ReverseSortedNodeType<Is>>(fn), ...);
        }
    };

} // namespace MQEngine

#endif // MQENGINE_BASE_STATIC_TOKEN_GRAPH_H
