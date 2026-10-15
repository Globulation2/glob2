// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cassert>
#include <cstddef>
#include <iterator>

// Owner-thread population membership. The ID table is deliberately separate.
// Entity hooks must be outside the state records copied to worker snapshots.
template<class T> class IntrusiveEntityList
{
public:
    class Cursor;
private:
    T* first = nullptr;
    T* last = nullptr;
    std::size_t count = 0;
    Cursor* cursors = nullptr;
public:
    IntrusiveEntityList() = default;
    IntrusiveEntityList(const IntrusiveEntityList&) = delete;
    IntrusiveEntityList& operator=(const IntrusiveEntityList&) = delete;
    ~IntrusiveEntityList() { assert(empty() && !cursors); }

    std::size_t size() const { return count; }
    bool empty() const { return !first; }
    T* front() const { return first; }
    T* back() const { return last; }

    class Iterator
    {
        T* value = nullptr;
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T*;
        using difference_type = std::ptrdiff_t;
        using reference = T*;
        using pointer = T**;
        Iterator() = default;
        explicit Iterator(T* value):value(value) {}
        T* operator*() const { return value; }
        Iterator& operator++() { value=value->populationNext; return *this; }
        Iterator operator++(int) { auto old=*this; ++*this; return old; }
        bool operator==(const Iterator&) const = default;
    };
    struct Range
    {
        const IntrusiveEntityList* list;
        Iterator begin() const { return Iterator(list->first); }
        Iterator end() const { return {}; }
        T* front() const { return list->first; }
        T* back() const { return list->last; }
        std::size_t size() const { return list->size(); }
        bool empty() const { return list->empty(); }
    };
    // Read-only walks must not change population membership. Mutable update
    // walks use Cursor, whose pending item and original fence are repaired.
    Range entries() const { return {this}; }
    Iterator begin() const { return Iterator(first); }
    Iterator end() const { return {}; }

    class Cursor
    {
        friend class IntrusiveEntityList;
        IntrusiveEntityList& list;
        Cursor* previous;
        Cursor* nextCursor = nullptr;
        T* pending;
        T* boundary;
    public:
        explicit Cursor(IntrusiveEntityList& list)
            :list(list),previous(nullptr),pending(list.first),boundary(list.last)
        {
            nextCursor=list.cursors;
            if (nextCursor) nextCursor->previous=this;
            list.cursors=this;
        }
        Cursor(const Cursor&) = delete;
        Cursor& operator=(const Cursor&) = delete;
        ~Cursor()
        {
            if (previous) previous->nextCursor=nextCursor;
            else list.cursors=nextCursor;
            if (nextCursor) nextCursor->previous=previous;
        }
        T* next()
        {
            T* result=pending;
            if (result) {
                pending=result==boundary ? nullptr : result->populationNext;
                if (!pending) boundary=nullptr;
            }
            return result;
        }
    };
    Cursor mutableWalk() { return Cursor(*this); }

    void append(T& item)
    {
        assert(!item.populationMembership && !item.populationPrevious && !item.populationNext);
        assert((first==nullptr)==(last==nullptr));
        assert(!last || !last->populationNext);
        item.populationMembership=this;
        item.populationPrevious=last;
        if (last) last->populationNext=&item;
        else first=&item;
        last=&item;
        ++count;
    }
    void remove(T& item)
    {
        assert(item.populationMembership==this);
        assert(count>0);
        assert(item.populationPrevious ? item.populationPrevious->populationNext==&item : first==&item);
        assert(item.populationNext ? item.populationNext->populationPrevious==&item : last==&item);
        for (Cursor* cursor=cursors; cursor; cursor=cursor->nextCursor) {
            if (cursor->pending==&item)
                cursor->pending=&item==cursor->boundary ? nullptr : item.populationNext;
            if (cursor->boundary==&item) cursor->boundary=item.populationPrevious;
            if (!cursor->pending) cursor->boundary=nullptr;
        }
        if (item.populationPrevious) item.populationPrevious->populationNext=item.populationNext;
        else first=item.populationNext;
        if (item.populationNext) item.populationNext->populationPrevious=item.populationPrevious;
        else last=item.populationPrevious;
        item.populationPrevious=item.populationNext=nullptr;
        item.populationMembership=nullptr;
        --count;
    }
    void clear() { while (first) remove(*first); }

    // Deliberately capacity-based: explicit deep diagnostics, never a tick path.
    bool matches(T* const* table, std::size_t capacity) const
    {
        std::size_t occupied=0,visited=0;
        for (std::size_t i=0;i<capacity;++i) if (table[i]) ++occupied;
        T* previous=nullptr;
        for (T* item=first;item;item=item->populationNext) {
            if (++visited>capacity || item->populationMembership!=this
                || item->populationPrevious!=previous) return false;
            const auto slot=T::GIDtoID(item->gid);
            if (slot>=capacity || table[slot]!=item) return false;
            previous=item;
        }
        return visited==count && occupied==count && previous==last;
    }
};
