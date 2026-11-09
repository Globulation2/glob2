// SPDX-License-Identifier: GPL-3.0-or-later
#include "ParallelPresentationWork.h"
#include "ThreadSupport.h"
#include <algorithm>
#include <system_error>
#include <utility>

namespace GAGCore
{
ParallelPresentationWork::ParallelPresentationWork(unsigned participants)
{
    if constexpr (!ThreadSupport::available)
        return;
    if (!participants)
        participants = std::max(1u, std::thread::hardware_concurrency());
    try
    {
        for (unsigned i = 1; i < participants; ++i)
            workers.push_back(ThreadSupport::launch([this] { work(); }));
    }
    catch (const std::system_error &)
    {
        // Fewer threads only narrows batches; the owner always takes part.
    }
}

ParallelPresentationWork::~ParallelPresentationWork()
{
    {
        std::lock_guard lock(mutex);
        stopping = true;
    }
    wake.notify_all();
    for (auto &worker : workers)
        worker.join();
}

// Claims jobs of the current batch until none remain. Called with the lock held.
void ParallelPresentationWork::drain()
{
    std::unique_lock lock(mutex, std::adopt_lock);
    while (job && next < count)
    {
        const auto index = next++;
        const auto *function = job;
        ++running;
        lock.unlock();
        std::exception_ptr failure;
        try
        {
            (*function)(index);
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        lock.lock();
        if (failure && !error)
        {
            error = failure;
            next = count; // Start no further jobs once one has failed.
        }
        if (--running == 0 && next >= count)
            done.notify_all();
    }
    lock.release();
}

void ParallelPresentationWork::work()
{
    std::unique_lock lock(mutex);
    std::uint64_t seen = 0;
    for (;;)
    {
        wake.wait(lock, [&] { return stopping || (batch != seen && job && next < count); });
        if (stopping)
            return;
        seen = batch;
        lock.release();
        drain();
        lock = std::unique_lock(mutex, std::adopt_lock);
    }
}

void ParallelPresentationWork::executePresentationJobs(std::size_t jobs,
                                                       const std::function<void(std::size_t)> &function)
{
    if (!jobs)
        return;
    if (workers.empty() || jobs == 1)
    {
        for (std::size_t i = 0; i < jobs; ++i)
            function(i);
        return;
    }
    std::unique_lock lock(mutex);
    job = &function;
    next = 0;
    count = jobs;
    error = nullptr;
    ++batch;
    wake.notify_all();
    lock.release();
    drain();
    lock = std::unique_lock(mutex, std::adopt_lock);
    done.wait(lock, [&] { return next >= count && running == 0; });
    job = nullptr;
    if (auto failure = std::exchange(error, nullptr))
        std::rethrow_exception(failure);
}
} // namespace GAGCore
