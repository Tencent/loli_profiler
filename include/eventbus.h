#ifndef EVENTBUS_H
#define EVENTBUS_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// Qt-free event delivery (replaces Qt signals/slots + the Qt event loop).
//
// Core processes and worker threads post type-erased events; the consumer
// (GUI frame loop / CLI loop) drains them on its own thread via Pump().
// Handlers therefore always run on the consumer thread - the same delivery
// guarantee QCoreApplication::processEvents() gave the previous Qt-based
// call sites.
//
// Usage:
//   EventBus bus;                          // one per application
//   bus.Subscribe("records.ready",
//       [](const Event& e) { ... });       // from the consumer thread
//   bus.Post("records.ready", payload);    // from any thread
//   bus.Pump();                            // each frame / loop iteration
//
// Handlers are stored by topic string (signal-name parity); the payload is
// an opaque std::string (binary-safe). Keep payloads small - for large data
// pass a pointer/id through the payload and hand over ownership elsewhere.
class EventBus {
public:
    struct Event {
        std::string topic;
        std::string payload;
        // Monotonic sequence number, set at Post() time.
        uint64_t seq = 0;
    };

    using Handler = std::function<void(const Event&)>;

    EventBus() = default;
    ~EventBus() = default;

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // Registers a handler for a topic. Handlers must be registered from the
    // consumer thread (before the pump loop starts delivering); Pump() never
    // mutates the subscription table. Multiple handlers per topic are
    // allowed and run in registration order.
    void Subscribe(const std::string& topic, Handler handler);

    // Removes all handlers for a topic.
    void Unsubscribe(const std::string& topic);

    // Posts an event. Safe from any thread. Events for topics with no
    // subscribers are still queued (a later Subscribe then a Pump sees
    // them); use topic naming discipline instead, or clear stale queues
    // manually.
    void Post(const std::string& topic, const std::string& payload = {});

    // Delivers all queued events to their handlers on the calling thread.
    // Call once per GUI frame / CLI loop iteration. Events posted by
    // handlers during Pump() are queued for the next Pump() (no recursion).
    void Pump();

    // Removes all queued events without delivering (e.g. session reset).
    void ClearPending();

    // Number of events waiting to be delivered (diagnostics).
    std::size_t PendingCount();

    // ----- Periodic dispatch (QTimer parity for the CLI loop) -----

    // Schedules a callback to run on the consumer thread every interval,
    // starting after the first interval elapses. Returns an id for Cancel.
    // interval of zero means "every Pump()" (used as the fixed-update tick).
    uint64_t ScheduleEvery(std::chrono::milliseconds interval, std::function<void()> callback);

    // Schedules a one-shot callback on the consumer thread after delay.
    // Returns an id for Cancel.
    uint64_t ScheduleAfter(std::chrono::milliseconds delay, std::function<void()> callback);

    // Cancels a periodic/one-shot schedule by id.
    void Cancel(uint64_t id);

private:
    struct Subscription {
        std::string topic;
        Handler handler;
    };
    struct Schedule {
        uint64_t id;
        std::chrono::milliseconds interval; // 0 = every Pump()
        std::chrono::milliseconds delay;    // one-shot remaining time
        bool once;
        std::function<void()> callback;
        std::chrono::steady_clock::time_point nextDue;
        bool dueInitialized = false;
    };

    void RunSchedulesLocked();

    // Subscription table: written only by Subscribe/Unsubscribe from the
    // consumer thread; read-only during Pump.
    std::vector<Subscription> subscriptions_;

    // Event queue: written by Post() from any thread; drained by Pump().
    std::mutex queueMutex_;
    std::vector<Event> queue_;
    uint64_t nextSeq_ = 0;

    // Periodic schedules: touched only on the consumer thread (Subscribe
    // side is Schedule*/Cancel; delivery happens inside Pump()).
    std::vector<Schedule> schedules_;
    uint64_t nextScheduleId_ = 1;
};

#endif // EVENTBUS_H
