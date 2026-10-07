// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ComputeExecutor.h"
#include <chrono>
#include "SceneInputs.h"
#include "render/scene/SceneBuffer.h"

// Presentation scheduling also works for hosts that keep simulation and network
// polling on the main thread. Only submit's immutable inputs reach the worker.
// Cancellation does not block the browser event loop. An active chunk retains
// its own state and immutable inputs until it returns.
class ScenePreparation
{
    ComputeExecutor& executor;
    struct State
    {
        SceneExtractor extractor;
        SceneBuffer<PresentationFrame> scenes;
    };
    std::shared_ptr<State> state=std::make_shared<State>();
    ComputeExecutor::PresentationTicket ticket;
    bool haveScene = false;
public:
    explicit ScenePreparation(ComputeExecutor& executor) : executor(executor) {}
    ~ScenePreparation() { if (ticket) ticket->cancel(); }
    ScenePreparation(const ScenePreparation&) = delete;
    ScenePreparation& operator=(const ScenePreparation&) = delete;

    bool readyToCapture() const
    {
        if (ticket && ticket->finished()) ticket->rethrowFailure();
        return !state->scenes.pending() && (!ticket || ticket->finished());
    }
    void submit(std::shared_ptr<const SceneInputs> input)
    {
        const auto chunks = SceneExtractor::preparationChunks(*input);
        ticket = executor.submitResumablePresentation(chunks, [state = state, input = std::move(input), chunks](size_t chunk) {
            if (!state->extractor.prepareChunk(*input, state->scenes.back(), chunk)) return false;
            if (chunk + 1 == chunks) state->scenes.publish();
            return true;
        });
    }
    const PresentationFrame* acquire(bool* changed = nullptr)
    {
        // Explicit fallback for builds/hosts without compute workers.
        const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(2);
        while (executor.pumpPresentation() && std::chrono::steady_clock::now()<until) {}
        if (ticket && ticket->finished()) ticket->rethrowFailure();
        const bool acquired=state->scenes.acquire();
        if (changed) *changed=acquired;
        if (acquired) haveScene = true;
        return haveScene ? &state->scenes.current() : nullptr;
    }
};
