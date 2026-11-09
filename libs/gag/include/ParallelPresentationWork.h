// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "PresentationWork.h"
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace GAGCore
{
// Presentation work for an offline capture: no simulation shares the CPUs, so
// every batch spreads over a persistent pool as wide as the machine. The owner
// takes part in each batch and returns only after every job has finished.
// Jobs write disjoint outputs fixed before submission, so the result is the
// same at every width; a job's exception is rethrown on the owner.
class ParallelPresentationWork : public PresentationWork
{
  public:
    // Zero sizes the pool from the hardware; one runs every batch on the owner.
    explicit ParallelPresentationWork(unsigned participants = 0);
    ~ParallelPresentationWork() override;
    ParallelPresentationWork(const ParallelPresentationWork &) = delete;
    ParallelPresentationWork &operator=(const ParallelPresentationWork &) = delete;

    void executePresentationJobs(std::size_t count,
                                 const std::function<void(std::size_t)> &job) override;
    // Owner plus workers.
    unsigned participants() const { return unsigned(workers.size()) + 1; }

  private:
    void work();
    void drain();

    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable wake, done;
    const std::function<void(std::size_t)> *job = nullptr;
    std::size_t next = 0, count = 0, running = 0;
    std::uint64_t batch = 0;
    std::exception_ptr error;
    bool stopping = false;
};

// Installs a pool for one offline capture on the calling (graphics owner)
// thread: batches run in parallel at the pool's width while it is in scope.
class OfflineRenderScope
{
    ParallelPresentationWork pool;
    PresentationWorkScope work;
    RenderExecutionScope execution;

  public:
    explicit OfflineRenderScope(unsigned participants = 0)
        : pool(participants), work(pool),
          execution(pool.participants() > 1 ? RenderExecution::Parallel : RenderExecution::Serial,
                    pool.participants())
    {
    }
    unsigned participants() const { return pool.participants(); }
};
} // namespace GAGCore
