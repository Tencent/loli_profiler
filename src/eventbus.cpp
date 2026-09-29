#include "eventbus.h"

#include <algorithm>

namespace {

std::chrono::steady_clock::time_point Now() {
    return std::chrono::steady_clock::now();
}

} // namespace

void EventBus::Subscribe(const std::string& topic, Handler handler) {
    subscriptions_.push_back(Subscription{topic, std::move(handler)});
}

void EventBus::Unsubscribe(const std::string& topic) {
    subscriptions_.erase(
        std::remove_if(subscriptions_.begin(), subscriptions_.end(),
                       [&topic](const Subscription& s) { return s.topic == topic; }),
        subscriptions_.end());
}

void EventBus::Post(const std::string& topic, const std::string& payload) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.push_back(Event{topic, payload, nextSeq_++});
}

void EventBus::Pump() {
    // Snapshot the queue: handlers may Post(), which must land in the next
    // Pump() rather than recursing inside this one.
    std::vector<Event> batch;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        batch.swap(queue_);
    }

    for (const Event& event : batch) {
        for (const Subscription& sub : subscriptions_) {
            if (sub.topic == event.topic)
                sub.handler(event);
        }
    }

    // Periodic / one-shot schedules (consumer thread only).
    if (!schedules_.empty())
        RunSchedulesLocked();
}

void EventBus::ClearPending() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.clear();
}

std::size_t EventBus::PendingCount() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return queue_.size();
}

uint64_t EventBus::ScheduleEvery(std::chrono::milliseconds interval,
                                 std::function<void()> callback) {
    Schedule s;
    s.id = nextScheduleId_++;
    s.interval = interval;
    s.once = false;
    s.callback = std::move(callback);
    s.nextDue = Now() + interval;
    schedules_.push_back(std::move(s));
    return schedules_.back().id;
}

uint64_t EventBus::ScheduleAfter(std::chrono::milliseconds delay,
                                 std::function<void()> callback) {
    Schedule s;
    s.id = nextScheduleId_++;
    s.interval = delay; // unused for one-shots beyond initial due
    s.once = true;
    s.callback = std::move(callback);
    s.nextDue = Now() + delay;
    schedules_.push_back(std::move(s));
    return schedules_.back().id;
}

void EventBus::Cancel(uint64_t id) {
    schedules_.erase(
        std::remove_if(schedules_.begin(), schedules_.end(),
                       [id](const Schedule& s) { return s.id == id; }),
        schedules_.end());
}

void EventBus::RunSchedulesLocked() {
    const auto now = Now();
    // Run due callbacks; erase finished one-shots. Iterate by index since
    // callbacks may call Cancel()/Schedule*() and mutate the vector.
    for (std::size_t i = 0; i < schedules_.size();) {
        Schedule& s = schedules_[i];
        if (s.nextDue <= now) {
            if (s.once) {
                // Copy the callback out: running it may Cancel() this id
                // (erasing from the vector) before the call finishes.
                auto callback = s.callback;
                schedules_.erase(schedules_.begin() + static_cast<std::ptrdiff_t>(i));
                callback();
                continue; // do not advance: element at i is the next one
            }
            // Periodic: recompute the next due time. A zero interval fires
            // every Pump().
            auto callback = s.callback;
            if (s.interval.count() > 0) {
                s.nextDue = now + s.interval;
                callback();
            } else {
                callback();
                s.nextDue = now;
            }
        }
        ++i;
    }
}
