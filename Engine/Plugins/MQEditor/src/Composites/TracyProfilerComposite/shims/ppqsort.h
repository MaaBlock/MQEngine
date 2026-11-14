#pragma once

#include <algorithm>
#include "tracy_pdqsort.h"

namespace ppqsort
{
    namespace execution
    {
        struct par_t {};
        inline constexpr par_t par{};
        struct seq_t {};
        inline constexpr seq_t seq{};
    } // namespace execution

    template<typename Policy, typename Iter>
    inline void sort(Policy, Iter first, Iter last)
    {
        tracy::pdqsort_branchless(first, last);
    }

    template<typename Policy, typename Iter, typename Compare>
    inline void sort(Policy, Iter first, Iter last, Compare comp)
    {
        tracy::pdqsort_branchless(first, last, comp);
    }

    template<typename Iter>
    inline void sort(Iter first, Iter last)
    {
        tracy::pdqsort_branchless(first, last);
    }

    template<typename Iter, typename Compare>
    inline void sort(Iter first, Iter last, Compare comp)
    {
        tracy::pdqsort_branchless(first, last, comp);
    }
} // namespace ppqsort
