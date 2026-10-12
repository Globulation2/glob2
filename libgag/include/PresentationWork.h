// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <cstdlib>
#include <algorithm>
#include <cstring>

namespace GAGCore
{
enum class RenderExecution
{
    Serial,
    Parallel,
    Auto
};
inline RenderExecution renderExecution()
{
    const char *mode = std::getenv("GLOB2_RENDER_EXECUTION");
    if (!mode || !std::strcmp(mode, "auto"))
        return RenderExecution::Auto;
    if (!std::strcmp(mode, "serial"))
        return RenderExecution::Serial;
    if (!std::strcmp(mode, "parallel"))
        return RenderExecution::Parallel;
    throw std::invalid_argument("GLOB2_RENDER_EXECUTION must be serial, parallel or auto");
}
inline std::size_t renderConcurrency()
{
    const char *value = std::getenv("GLOB2_RENDER_WORKERS");
    if (!value)
        return 2;
    char *end = nullptr;
    const auto count = std::strtoul(value, &end, 10);
    if (end == value || *end || count < 1 || count > 64)
        throw std::invalid_argument("GLOB2_RENDER_WORKERS must be between 1 and 64");
    return std::min(count, 2ul);
}
// Synchronous CPU work only. The graphics owner retains all inputs and waits
// before touching outputs or calling its backend again. Callbacks never join.
class PresentationWork
{
  public:
    virtual ~PresentationWork() = default;
    virtual void executePresentationJobs(std::size_t count,
                                         const std::function<void(std::size_t)> &job) = 0;
    inline static thread_local PresentationWork *current = nullptr;
};
class PresentationWorkScope
{
    PresentationWork *previous;

  public:
    explicit PresentationWorkScope(PresentationWork &work) : previous(PresentationWork::current)
    {
        PresentationWork::current = &work;
    }
    ~PresentationWorkScope() { PresentationWork::current = previous; }
    PresentationWorkScope(const PresentationWorkScope &) = delete;
    PresentationWorkScope &operator=(const PresentationWorkScope &) = delete;
};
inline void executePresentationJobs(std::size_t count, const std::function<void(std::size_t)> &job)
{
    if (renderExecution() == RenderExecution::Parallel && PresentationWork::current)
        PresentationWork::current->executePresentationJobs(count, job);
    else
        for (std::size_t i = 0; i < count; ++i)
            job(i);
}
} // namespace GAGCore
