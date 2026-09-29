#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

// Qt-free parallel work facility (replaces QtConcurrent::run / QFuture /
// QFutureWatcher for the core's translation and load workloads).
//
// Usage patterns (mirroring the previous call sites):
//  - Fan-out + blocking join (CLI symbol translation): submit N tasks,
//    hold the returned futures, call .get() on each.
//      ThreadPool pool;
//      auto f1 = pool.Submit([]{ return Translate(range1); });
//      auto f2 = pool.Submit([]{ return Translate(range2); });
//      auto a = f1.get(); auto b = f2.get();
//  - Fire-and-notify (GUI record load): submit one task returning a
//    result, poll/notify completion from the consumer loop (the GUI
//    already polls load state each frame).
//
// Tasks returning void are supported. The pool owns a persistent set of
// worker threads (no thread churn across large loads); worker count
// defaults to the hardware concurrency (QThread::idealThreadCount parity,
// minimum 2 to match the previous call site).
class ThreadPool {
public:
    // Creates a pool with max(2, hardware_concurrency) workers.
    ThreadPool();
    explicit ThreadPool(unsigned int workerCount);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Queues fn() for execution on a worker. Returns a future for its
    // return value. Exceptions propagate through the future (std::packaged_task).
    template <typename Fn>
    auto Submit(Fn fn) -> std::future<decltype(fn())> {
        using R = decltype(fn());
        auto task = std::make_shared<std::packaged_task<R()>>(std::move(fn));
        std::future<R> future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push([task]() { (*task)(); });
        }
        cv_.notify_one();
        return future;
    }

    // Number of worker threads in the pool.
    unsigned int WorkerCount() const { return static_cast<unsigned int>(workers_.size()); }

    // True when no tasks are queued or running.
    bool Idle();
    // Blocks until all submitted tasks finish. Call before destroying state
    // referenced by any task that captures the pool's owner.
    void WaitIdle();

private:
    void WorkerLoop(unsigned int index);

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable idleCv_;
    std::atomic<bool> stop_{false};
    std::atomic<uint32_t> activeTasks_{0};
};

#endif // THREADPOOL_H
