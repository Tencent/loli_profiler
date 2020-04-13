#pragma once
#include <atomic>

class spinlock {
public:
    spinlock() { m_lock.clear(); }
    spinlock(const spinlock&) = delete;
    ~spinlock() = default;

    void lock() {
        while (m_lock.test_and_set(std::memory_order_acquire));
    }
    bool try_lock() {
        return !m_lock.test_and_set(std::memory_order_acquire);
    }
    void unlock() {
        m_lock.clear(std::memory_order_release);
    }
private:
    std::atomic_flag m_lock;
};
