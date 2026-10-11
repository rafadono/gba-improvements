#pragma once
// A copyable, implicitly convertible wrapper around std::atomic<T> with relaxed ordering.
//
// Settings structs that an audio thread reads while a UI/game thread writes them use it
// for plain scalar fields (bool / int / float). It keeps the old field-style API
// (`s.enabled = true; if (s.enabled) ...; float g = s.gain;`) while making each access
// a data-race-free atomic load/store. Relaxed ordering is enough: every field is
// independent and the consumer only needs *some* recent value of each.
#include <atomic>

namespace gbaimp {

template <typename T>
class Relaxed {
public:
    Relaxed() noexcept : v_(T{}) {}
    Relaxed(T v) noexcept : v_(v) {}                                   // NOLINT: implicit on purpose
    Relaxed(const Relaxed& o) noexcept : v_(o.load()) {}
    Relaxed& operator=(const Relaxed& o) noexcept { store(o.load()); return *this; }
    Relaxed& operator=(T v) noexcept { store(v); return *this; }
    operator T() const noexcept { return load(); }                     // NOLINT: implicit on purpose

    T load() const noexcept { return v_.load(std::memory_order_relaxed); }
    void store(T v) noexcept { v_.store(v, std::memory_order_relaxed); }

private:
    std::atomic<T> v_;
};

} // namespace gbaimp
