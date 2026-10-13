
// Append to the unchanged ComputeExecutorHarness.cpp; existing includes suffice.
// Public behavior only: the same fixture must pass baseline and candidate.
TEST_CASE("presentation priority discards deferred selection before slot reuse and generation" * doctest::test_suite("ComputeExecutor"))
{
    if constexpr (!GAGCore::ThreadSupport::available) return;
    for (auto mode : {ComputeTelemetryMode::Off, ComputeTelemetryMode::Waits, ComputeTelemetryMode::Trace})
    for (unsigned threads : {3u, 6u, 12u})
    {
        CAPTURE(threads, unsigned(mode));
        struct State {
            std::array<std::atomic<bool>, 11> entered{}, open{};
            std::array<std::thread::id, 11> workerIds{};
            std::array<std::atomic<unsigned>, 12> oldCalls{};
            std::array<std::atomic<unsigned>, 2> newCalls{}, generationCalls{};
            std::atomic<bool> identified{false}, oldGate{false}, arm{false};
            std::atomic<bool> presentationEntered{false}, presentationOpen{false};
            std::atomic<bool> newEntered{false}, newOpen{false};
            std::atomic<bool> generationEntered{false}, generationOpen{false};
            std::atomic<bool> done{false}, timedOut{false}, wrongPresentationThread{false};
            std::thread::id presentationId;
            std::mutex mutex;
            std::vector<size_t> oldOrder, newOrder;
            void release() {
                for (auto& gate : open) gate = true;
                arm = true; presentationOpen = true; newOpen = true; generationOpen = true;
            }
        } state;
        // State outlives executor, including all exceptional/unwind paths.
        ComputeExecutor executor; executor.configureTelemetry(mode); executor.configure(threads);
        CHECK(executor.threadCount() == threads);
        if (executor.threadCount() != threads) continue;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        struct ReleaseGuard {
            State& state; std::thread watchdog;
            ReleaseGuard(State& state, std::chrono::steady_clock::time_point deadline) : state(state), watchdog([&state, deadline] {
                while (!state.done && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
                if (!state.done) { state.timedOut = true; state.release(); }
            }) {}
            ~ReleaseGuard() { state.release(); state.done = true; watchdog.join(); }
        } releaseGuard(state, deadline);
        const auto wait = [&](auto predicate) {
            while (!predicate() && !state.timedOut) std::this_thread::yield();
            return predicate() && !state.timedOut;
        };
        auto identify = executor.submitPresentation(1, [&](size_t) {
            state.presentationId = std::this_thread::get_id(); state.identified = true;
        });
        if (!wait([&] { return state.identified.load() && identify->finished(); })) { CHECK_FALSE(state.timedOut); continue; }
        ComputeExecutor::Group holds{threads - 1, {[](void* p, size_t i) {
            auto& s = *static_cast<State*>(p);
            s.workerIds[i] = std::this_thread::get_id(); s.entered[i] = true;
            while (!s.open[i]) std::this_thread::yield();
        }, &state}};
        auto held = executor.submit(std::span(&holds, 1));
        if (!wait([&] { for (unsigned i = 0; i < threads - 1; ++i) if (!state.entered[i]) return false; return true; })) { CHECK_FALSE(state.timedOut); continue; }
        size_t designated = threads;
        for (unsigned i = 0; i < threads - 1; ++i) if (state.workerIds[i] == state.presentationId) designated = i;
        CHECK(designated < threads - 1);
        if (designated >= threads - 1) continue;
        // Only the designated worker can take these first nine lane jobs.
        ComputeExecutor::Group oldGroup{12, {[](void* p, size_t i) {
            auto& s = *static_cast<State*>(p); ++s.oldCalls[i];
            { std::lock_guard lock(s.mutex); s.oldOrder.push_back(i); }
            if (i == 8) { s.oldGate = true; while (!s.arm) std::this_thread::yield(); }
        }, &state}, 3};
        auto oldBatch = executor.submit(std::span(&oldGroup, 1), 1);
        state.open[designated] = true;
        if (!wait([&] { return state.oldGate.load(); })) { CHECK_FALSE(state.timedOut); continue; }
        auto presentation = executor.submitPresentation(1, [&](size_t) {
            state.wrongPresentationThread = std::this_thread::get_id() != state.presentationId;
            state.presentationEntered = true;
            while (!state.presentationOpen) std::this_thread::yield();
        });
        state.arm = true;
        if (!wait([&] { return state.presentationEntered.load(); })) { CHECK_FALSE(state.timedOut); continue; }
        CHECK_FALSE(state.wrongPresentationThread);
        CHECK_FALSE(presentation->finished());
        for (unsigned i = 0; i < 9; ++i) CHECK(state.oldCalls[i].load() == 1);
        CHECK(state.oldCalls[9].load() == 0);
        // General workers finish the old lane while the designated callback is held.
        for (unsigned i = 0; i < threads - 1; ++i) if (i != designated) state.open[i] = true;
        executor.join(oldBatch); // Free its slot; held batch keeps the earlier slot live.
        ComputeExecutor::Group replacement{2, {[](void* p, size_t i) {
            auto& s = *static_cast<State*>(p); ++s.newCalls[i];
            { std::lock_guard lock(s.mutex); s.newOrder.push_back(i); }
            if (i == 0) { s.newEntered = true; while (!s.newOpen) std::this_thread::yield(); }
        }, &state}, 3};
        auto newBatch = executor.submit(std::span(&replacement, 1), 2);
        if (!wait([&] { return state.newEntered.load(); })) { CHECK_FALSE(state.timedOut); continue; }
        // Start a blocking generation while the presentation callback still owns
        // its inputs. A generation callback is held until the releaser observes
        // invocation, then opens both callback gates without timing sleeps.
        std::thread releaser([&] {
            if (wait([&] { return state.generationEntered.load(); })) {
                state.presentationOpen = true; state.newOpen = true; state.generationOpen = true;
            } else state.release();
        });
        struct JoinReleaser { std::thread& thread; State& state; ~JoinReleaser() { state.release(); thread.join(); } } joinReleaser{releaser, state};
        executor.run(2, [&](size_t i) {
            ++state.generationCalls[i];
            state.generationEntered = true;
            while (!state.generationOpen) std::this_thread::yield();
        });
        executor.join(newBatch); executor.join(held); executor.cancelPresentationAndWait();
        CHECK(state.generationEntered.load());
        CHECK_FALSE(state.timedOut.load());
        for (auto& calls : state.oldCalls) CHECK(calls.load() == 1);
        for (auto& calls : state.newCalls) CHECK(calls.load() == 1);
        for (auto& calls : state.generationCalls) CHECK(calls.load() == 1);
        CHECK(state.oldOrder == std::vector<size_t>{0,1,2,3,4,5,6,7,8,9,10,11});
        CHECK(state.newOrder == std::vector<size_t>{0,1});
        CHECK(executor.liveBatches() == 0);
        const auto snapshot = executor.telemetrySnapshot(mode == ComputeTelemetryMode::Trace);
        CHECK(snapshot.available == (mode != ComputeTelemetryMode::Off));
        if (mode == ComputeTelemetryMode::Trace) {
            std::array<unsigned, 12> oldClaims{};
            std::array<unsigned, 2> replacementClaims{};
            for (const auto& stream : snapshot.streams) {
                CHECK(stream.lost == 0);
                for (std::size_t i = 0; i < stream.used; ++i) {
                    const auto& event = stream.storage[i];
                    if (event.kind != ComputeTelemetry::EventKind::Claim || event.executionKind != 0 || event.lane != 3) continue;
                    CHECK(event.thread != 0);
                    if (event.due == 1) { REQUIRE(event.job < oldClaims.size()); ++oldClaims[event.job]; }
                    if (event.due == 2) { REQUIRE(event.job < replacementClaims.size()); ++replacementClaims[event.job]; }
                }
            }
            for (auto count : oldClaims) CHECK(count == 1);
            for (auto count : replacementClaims) CHECK(count == 1);
        }
    }
}
