#pragma once
#include <cstddef>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core::Ring<T> -- a fixed-capacity history buffer. One per metric.
//
// Push overwrites the oldest once full; nothing ever allocates after
// construction, so a monitor left running for a week holds exactly the memory
// it held after a minute. Index 0 is the OLDEST kept sample, size()-1 the
// newest -- the order a graph draws left to right.
//
// Copyable on purpose: the Sampler copies its rings into each published
// Snapshot so the UI reads history without a lock (a few hundred floats per
// metric -- cheap at 1 Hz).
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::core {

template <class T>
class Ring {
public:
    explicit Ring(std::size_t capacity = 300) : buf_(capacity ? capacity : 1) {}

    void push(const T& v) {
        buf_[head_] = v;
        head_ = (head_ + 1) % buf_.size();
        if (n_ < buf_.size()) ++n_;
    }
    void clear() { head_ = 0; n_ = 0; }

    std::size_t size()     const { return n_; }
    std::size_t capacity() const { return buf_.size(); }
    bool        empty()    const { return n_ == 0; }
    bool        full()     const { return n_ == buf_.size(); }

    // 0 = oldest, size()-1 = newest. Unchecked, like vector::operator[].
    const T& operator[](std::size_t i) const {
        return buf_[(head_ + buf_.size() - n_ + i) % buf_.size()];
    }
    const T& back()  const { return (*this)[n_ - 1]; }
    const T& front() const { return (*this)[0]; }

    // Oldest -> newest, as a plain vector (for graphs / export).
    std::vector<T> to_vector() const {
        std::vector<T> v;
        v.reserve(n_);
        for (std::size_t i = 0; i < n_; ++i) v.push_back((*this)[i]);
        return v;
    }

private:
    std::vector<T> buf_;
    std::size_t    head_ = 0;   // next write slot
    std::size_t    n_    = 0;   // samples held
};

}  // namespace trails::core
