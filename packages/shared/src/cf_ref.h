#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <utility>

namespace spacewing {
// RAII (scope-based ownership) wrapper for Apple's reference-counted Core Foundation
// objects, such as CFArrayRef and AXUIElementRef. Each non-null wrapper owns one
// reference and calls CFRelease when destroyed, including during exception unwinding.
// Copies retain the object; moves transfer ownership and leave the source empty.
//
// Construction adopts an already-owned reference without retaining it, typically
// a result from a Create/Copy API. Retain a borrowed reference before adopting it;
// otherwise this wrapper would release a reference that belongs to another owner.
// Both feature packages use this wrapper to manage native object lifetimes.
template <typename T> class CFRef {
public:
    CFRef() : value_(nullptr) {}
    explicit CFRef(T value) : value_(value) {}
    ~CFRef() {
        if (value_)
            CFRelease(value_);
    }
    CFRef(const CFRef &other) : value_(other.value_) {
        if (value_)
            CFRetain(value_);
    }
    CFRef(CFRef &&other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    CFRef &operator=(CFRef other) noexcept {
        std::swap(value_, other.value_);
        return *this;
    }
    // Borrows the underlying pointer without transferring ownership or retaining it.
    // Retain it separately if it must outlive this wrapper's ownership.
    T get() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }

private:
    T value_;
};
} // namespace spacewing
