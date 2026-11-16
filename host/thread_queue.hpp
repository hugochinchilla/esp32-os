// The message queue for the PC: the same port as on the board, over the
// PC's threads, so the tests can post from real threads.
#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "core/msg/queue.hpp"

namespace host {

class ThreadQueue : public msg::Queue {
public:
    static constexpr int kDepth = 32; // as on the board

    bool post(const msg::Message &m, uint32_t wait_ms = msg::kNoWait) override
    {
        std::unique_lock<std::mutex> hold(mutex_);
        const auto has_room = [this] { return count_ < kDepth; };
        if (wait_ms == msg::kForever) {
            room_.wait(hold, has_room);
        } else if (!room_.wait_for(hold, std::chrono::milliseconds(wait_ms), has_room)) {
            refused_++;
            return false;
        }
        ring_[(first_ + count_) % kDepth] = m;
        count_++;
        most_ = count_ > most_ ? count_ : most_;
        woken_ = true;
        hold.unlock();
        wake_.notify_one();
        return true;
    }

    void wake() override
    {
        {
            const std::lock_guard<std::mutex> hold(mutex_);
            woken_ = true;
        }
        wake_.notify_one();
    }

    bool take(msg::Message &out) override
    {
        {
            const std::lock_guard<std::mutex> hold(mutex_);
            if (count_ == 0) {
                return false;
            }
            out = ring_[first_];
            first_ = (first_ + 1) % kDepth;
            count_--;
        }
        room_.notify_one();
        return true;
    }

    bool sleep(uint32_t max_us) override
    {
        std::unique_lock<std::mutex> hold(mutex_);
        wake_.wait_for(hold, std::chrono::microseconds(max_us), [this] { return woken_; });
        const bool woken = woken_;
        woken_ = false;
        return woken;
    }

    // For the tests.
    int waiting()
    {
        const std::lock_guard<std::mutex> hold(mutex_);
        return count_;
    }
    int most_waiting()
    {
        const std::lock_guard<std::mutex> hold(mutex_);
        return most_;
    }
    unsigned refused()
    {
        const std::lock_guard<std::mutex> hold(mutex_);
        return refused_;
    }

private:
    std::mutex mutex_;
    std::condition_variable wake_, room_;
    msg::Message ring_[kDepth] = {};
    int first_ = 0, count_ = 0, most_ = 0;
    unsigned refused_ = 0;
    bool woken_ = false;
};

} // namespace host
