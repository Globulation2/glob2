// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

namespace gradient_kernel
{
// Owned metadata, independent of Map/jobs/worker stacks. Owner cancellation
// never queries a driver or waits for a GPU event. Deadline uses monotonicNs().
class OpenCLRequiredControl
{
    std::atomic<bool> canceled{false};
    std::atomic<std::uint64_t> deadline{0};
    std::mutex waitMutex;
    std::condition_variable changed;
public:
    void requestCancel() noexcept {canceled.store(true,std::memory_order_release);changed.notify_all();}
    void setDeadlineNs(std::uint64_t value) noexcept {deadline.store(value,std::memory_order_release);changed.notify_all();}
    bool shouldCancel(std::uint64_t now) const noexcept {
        const auto until=deadline.load(std::memory_order_acquire);
        return canceled.load(std::memory_order_acquire) || (until && now>=until);
    }
    // Coordinator only. A second field's cancellation is observed at the next
    // bounded batch poll even if this control did not receive its notification.
    void waitFor(std::chrono::microseconds duration) {
        std::unique_lock lock(waitMutex);
        changed.wait_for(lock,duration,[&]{return canceled.load(std::memory_order_acquire);});
    }
};
// Pre-reserves object/control bytes in the shared host budget; null declines
// accelerator ownership before original seeds move. No driver initialization.
std::shared_ptr<OpenCLRequiredControl> makeOpenCLRequiredControl() noexcept;
}
