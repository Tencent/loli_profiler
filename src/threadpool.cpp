#include "threadpool.h"

#include <stdexcept>

ThreadPool::ThreadPool()
    : ThreadPool(std::max(2u, std::thread::hardware_concurrency())) {}

ThreadPool::ThreadPool(unsigned int workerCount) {
    if (workerCount == 0)
        workerCount = 1;
    workers_.reserve(workerCount);
    for (unsigned int i = 0; i < workerCount; ++i)
        workers_.emplace_back(&ThreadPool::WorkerLoop, this, i);
}

ThreadPool::~ThreadPool() {
    stop_.store(true, std::memory_order_release);
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable())
            worker.join();
    }
}

bool ThreadPool::Idle() {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.empty() && activeTasks_.load(std::memory_order_acquire) == 0;
}

void ThreadPool::WaitIdle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idleCv_.wait(lock, [this]() {
        return tasks_.empty() && activeTasks_.load(std::memory_order_acquire) == 0;
    });
}

void ThreadPool::WorkerLoop(unsigned int /*index*/) {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return stop_.load(std::memory_order_acquire) || !tasks_.empty();
            });
            if (stop_.load(std::memory_order_acquire) && tasks_.empty())
                return;
            task = std::move(tasks_.front());
            tasks_.pop();
            activeTasks_.fetch_add(1, std::memory_order_acq_rel);
        }

        task();

        const bool nowIdle = activeTasks_.fetch_sub(1, std::memory_order_acq_rel) == 1 &&
                             [this]() {
                                 std::lock_guard<std::mutex> lock(mutex_);
                                 return tasks_.empty();
                             }();
        if (nowIdle)
            idleCv_.notify_all();
    }
}
