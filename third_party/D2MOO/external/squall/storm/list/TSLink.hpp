#ifndef STORM_LIST_TS_LINK_HPP
#define STORM_LIST_TS_LINK_HPP

#include <cstddef>
#include <cstdint>

// End-of-list marker test. Storm marks a list end with the bitwise NOT of the terminator's
// address and tested "pointer <= 0", which only holds while user memory is below 0x80000000
// (32-bit Windows). Real nodes are aligned (even); the NOT of an aligned address is odd.
inline bool SquallIsListEnd(const void* p) {
    return p == nullptr || (reinterpret_cast<uintptr_t>(p) & 1) != 0;
}

template <class T>
class TSLink {
    public:
    // Member variables
    TSLink<T>* m_prevlink = nullptr;
    T* m_next = nullptr;

    // Member functions
    ~TSLink();
    bool IsLinked();
    T* Next();
    TSLink<T>* NextLink(ptrdiff_t linkoffset);
    T* Prev();
    T* RawNext();
    void Unlink();
};

template <class T>
TSLink<T>::~TSLink() {
    this->Unlink();
}

template <class T>
bool TSLink<T>::IsLinked() {
    return this->m_next != nullptr;
}

template <class T>
T* TSLink<T>::Next() {
    // Check for sentinel node (indicates list end)
    return SquallIsListEnd(this->m_next) ? nullptr : this->m_next;
}

template <class T>
TSLink<T>* TSLink<T>::NextLink(ptrdiff_t linkoffset) {
    T* next = this->m_next;

    if (SquallIsListEnd(next)) {
        // End of list
        return reinterpret_cast<TSLink<T>*>(~reinterpret_cast<uintptr_t>(next));
    } else {
        ptrdiff_t offset;

        if (linkoffset < 0) {
            offset = reinterpret_cast<uintptr_t>(this) - reinterpret_cast<uintptr_t>(this->m_prevlink->m_next);
        } else {
            offset = linkoffset;
        }

        return reinterpret_cast<TSLink<T>*>(reinterpret_cast<uintptr_t>(this->m_next) + offset);
    }
}

template <class T>
T* TSLink<T>::Prev() {
    return this->m_prevlink->m_prevlink->Next();
}

template <class T>
T* TSLink<T>::RawNext() {
    return this->m_next;
}

template <class T>
void TSLink<T>::Unlink() {
    if (this->m_prevlink) {
        this->NextLink(-1)->m_prevlink = this->m_prevlink;
        this->m_prevlink->m_next = this->m_next;

        this->m_prevlink = nullptr;
        this->m_next = nullptr;
    }
}

#endif
