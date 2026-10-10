// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnitConsts.h"
#include <array>
#include <vector>
#include <stdexcept>
#include <type_traits>
#include <iterator>

// Default games keep the three historical counters inline. Additional unit
// definitions allocate only the extra rows, once during match initialization.
template<class T> class UnitStatistics
{
    std::array<T,NB_UNIT_TYPE> builtin_{};
    std::vector<T> extra_;
public:
    std::size_t size() const { return NB_UNIT_TYPE+extra_.size(); }
    void resize(std::size_t count) {
        if (count<NB_UNIT_TYPE || count>1024) throw std::runtime_error("Invalid unit statistics type count");
        extra_.resize(count-NB_UNIT_TYPE);
    }
    T& operator[](std::size_t id) { return id<NB_UNIT_TYPE ? builtin_[id] : extra_.at(id-NB_UNIT_TYPE); }
    const T& operator[](std::size_t id) const { return id<NB_UNIT_TYPE ? builtin_[id] : extra_.at(id-NB_UNIT_TYPE); }
    void clear() { builtin_={}; for (auto& row:extra_) row={}; }
    std::size_t extraCapacityBytes() const { return extra_.capacity()*sizeof(T); }
    template<bool Const> struct Iterator {
        using Store=std::conditional_t<Const,const UnitStatistics,UnitStatistics>;
        using value_type=T;
        using difference_type=std::ptrdiff_t;
        using iterator_category=std::forward_iterator_tag;
        using reference=std::conditional_t<Const,const T&,T&>;
        using pointer=std::conditional_t<Const,const T*,T*>;
        Store* store=nullptr; std::size_t index=0;
        decltype(auto) operator*() const { return (*store)[index]; }
        Iterator& operator++() { ++index; return *this; }
        Iterator operator++(int) { auto old=*this;++*this;return old; }
        bool operator==(const Iterator&) const = default;
    };
    Iterator<false> begin() { return {this,0}; }
    Iterator<false> end() { return {this,size()}; }
    Iterator<true> begin() const { return {this,0}; }
    Iterator<true> end() const { return {this,size()}; }
    bool operator==(const UnitStatistics&) const = default;
};
