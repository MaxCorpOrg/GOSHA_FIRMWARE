#include "audio/audio_playback_drain_tracker.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

using namespace std::chrono_literals;

struct Harness {
    AudioPlaybackDrainTracker tracker;
    std::mutex mutex;
    std::condition_variable cv;
    bool service_stopped = false;
    bool decode_queue_empty = true;
    bool playback_queue_empty = true;

    bool CanDrainLocked() const {
        return service_stopped ||
            tracker.IsPlaybackDrained(decode_queue_empty, playback_queue_empty);
    }

    void WaitForDrain(std::atomic<bool>& returned, std::atomic<int>& order,
                      std::atomic<int>& sequence) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this]() { return CanDrainLocked(); });
        order.store(sequence.fetch_add(1) + 1);
        returned.store(true);
    }
};

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

void RequireStillBlocked(const std::atomic<bool>& returned, const char* message) {
    std::this_thread::sleep_for(50ms);
    Require(!returned.load(), message);
}

void RequireReturned(Harness& harness, std::thread& waiter,
                     const std::atomic<bool>& returned, const char* message) {
    for (int i = 0; i < 50; ++i) {
        if (returned.load()) {
            waiter.join();
            return;
        }
        std::this_thread::sleep_for(10ms);
    }
    {
        std::lock_guard<std::mutex> lock(harness.mutex);
        harness.service_stopped = true;
        harness.tracker.CancelQueuedPlayback();
    }
    harness.cv.notify_all();
    waiter.join();
    Require(false, message);
}

void TestDrainWaitsForDecodeOutputAndCallback() {
    Harness harness;
    std::atomic<bool> returned{false};
    std::atomic<int> sequence{0};
    std::atomic<int> callback_order{0};
    std::atomic<int> wait_order{0};

    uint32_t decode_generation = 0;
    {
        std::lock_guard<std::mutex> lock(harness.mutex);
        decode_generation = harness.tracker.BeginDecode();
    }

    std::thread waiter([&harness, &returned, &wait_order, &sequence]() {
        harness.WaitForDrain(returned, wait_order, sequence);
    });
    harness.cv.notify_all();
    RequireStillBlocked(returned, "drain returned while decode was in flight");

    {
        std::lock_guard<std::mutex> lock(harness.mutex);
        Require(harness.tracker.IsCurrent(decode_generation), "decode generation changed unexpectedly");
        harness.playback_queue_empty = false;
        harness.tracker.FinishDecode();
    }
    harness.cv.notify_all();
    RequireStillBlocked(returned, "drain returned while playback queue was non-empty");

    {
        std::lock_guard<std::mutex> lock(harness.mutex);
        harness.playback_queue_empty = true;
        harness.tracker.BeginPlayback();
    }
    harness.cv.notify_all();
    RequireStillBlocked(returned, "drain returned while output was in flight");

    callback_order.store(sequence.fetch_add(1) + 1);
    RequireStillBlocked(returned, "drain returned before output in-flight completion");

    {
        std::lock_guard<std::mutex> lock(harness.mutex);
        harness.tracker.FinishPlayback();
    }
    harness.cv.notify_all();
    RequireReturned(harness, waiter, returned, "drain did not return after output completed");
    Require(callback_order.load() > 0, "remote audio callback was not observed");
    Require(wait_order.load() > callback_order.load(), "drain returned before remote audio callback");
}

void TestResetDropsInFlightDecodeWithoutUnblockingEarly() {
    Harness harness;
    std::atomic<bool> returned{false};
    std::atomic<int> sequence{0};
    std::atomic<int> wait_order{0};

    uint32_t decode_generation = 0;
    {
        std::lock_guard<std::mutex> lock(harness.mutex);
        decode_generation = harness.tracker.BeginDecode();
        harness.tracker.CancelQueuedPlayback();
    }

    std::thread waiter([&harness, &returned, &wait_order, &sequence]() {
        harness.WaitForDrain(returned, wait_order, sequence);
    });
    harness.cv.notify_all();
    RequireStillBlocked(returned, "reset drain returned while decode was still in flight");

    {
        std::lock_guard<std::mutex> lock(harness.mutex);
        if (harness.tracker.IsCurrent(decode_generation)) {
            harness.playback_queue_empty = false;
        }
        harness.tracker.FinishDecode();
    }
    harness.cv.notify_all();
    RequireReturned(harness, waiter, returned, "reset drain did not return after stale decode completed");
    Require(wait_order.load() > 0, "reset drain waiter did not record completion");
}

void TestResetSuppressesStaleOutputCallback() {
    Harness harness;
    std::atomic<bool> returned{false};
    std::atomic<int> sequence{0};
    std::atomic<int> wait_order{0};
    int callback_count = 0;

    uint32_t playback_generation = 0;
    {
        std::lock_guard<std::mutex> lock(harness.mutex);
        playback_generation = harness.tracker.generation();
        harness.tracker.BeginPlayback();
        harness.tracker.CancelQueuedPlayback();
    }

    std::thread waiter([&harness, &returned, &wait_order, &sequence]() {
        harness.WaitForDrain(returned, wait_order, sequence);
    });
    harness.cv.notify_all();
    RequireStillBlocked(returned, "reset drain returned while output was still in flight");

    {
        std::lock_guard<std::mutex> lock(harness.mutex);
        if (harness.tracker.IsCurrent(playback_generation)) {
            callback_count++;
        }
        harness.tracker.FinishPlayback();
    }
    harness.cv.notify_all();
    RequireReturned(harness, waiter, returned, "reset drain did not return after stale output completed");
    Require(callback_count == 0, "stale output was allowed to trigger remote audio callback");
}

void TestStoppedServiceUnblocksTeardown() {
    Harness harness;
    std::atomic<bool> returned{false};
    std::atomic<int> sequence{0};
    std::atomic<int> wait_order{0};

    {
        std::lock_guard<std::mutex> lock(harness.mutex);
        harness.tracker.BeginPlayback();
    }

    std::thread waiter([&harness, &returned, &wait_order, &sequence]() {
        harness.WaitForDrain(returned, wait_order, sequence);
    });
    harness.cv.notify_all();
    RequireStillBlocked(returned, "teardown drain returned before service stop");

    {
        std::lock_guard<std::mutex> lock(harness.mutex);
        harness.service_stopped = true;
        harness.tracker.CancelQueuedPlayback();
    }
    harness.cv.notify_all();
    RequireReturned(harness, waiter, returned, "service stop did not unblock drain waiter");

    {
        std::lock_guard<std::mutex> lock(harness.mutex);
        harness.tracker.FinishPlayback();
    }
}

}  // namespace

int main() {
    TestDrainWaitsForDecodeOutputAndCallback();
    TestResetDropsInFlightDecodeWithoutUnblockingEarly();
    TestResetSuppressesStaleOutputCallback();
    TestStoppedServiceUnblocksTeardown();
    std::cout << "audio playback drain host test passed" << std::endl;
    return 0;
}
