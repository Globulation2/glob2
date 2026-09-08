// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <coroutine>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>
namespace GAGCore
{
// A move-only boolean job. advance() runs to the next explicit checkpoint,
// including checkpoints inside awaited child jobs. Destroying the root destroys
// every suspended child and its local resources. All use stays on one host thread.
class CooperativeTask
{
    struct State { std::coroutine_handle<> leaf; const char* stage = ""; };
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;
    struct promise_type
    {
        std::shared_ptr<State> state = std::make_shared<State>();
        std::coroutine_handle<> continuation = std::noop_coroutine();
        std::exception_ptr error;
        bool value = false;
        CooperativeTask get_return_object() {
            auto handle = Handle::from_promise(*this); state->leaf = handle;
            return CooperativeTask(handle);
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        struct Final {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(Handle handle) noexcept {
                auto& p = handle.promise(); p.state->leaf = p.continuation;
                return p.continuation;
            }
            void await_resume() noexcept {}
        };
        Final final_suspend() noexcept { return {}; }
        void return_value(bool result) noexcept { value = result; }
        void unhandled_exception() noexcept { error = std::current_exception(); }
    };
    explicit CooperativeTask(Handle handle) : handle(handle) {}
    CooperativeTask(CooperativeTask&& other) noexcept : handle(std::exchange(other.handle, {})) {}
    CooperativeTask(const CooperativeTask&) = delete;
    ~CooperativeTask() { if (handle) handle.destroy(); }
    bool advance() {
        if (!handle.done()) handle.promise().state->leaf.resume();
        return handle.done();
    }
    bool result() const {
        if (!handle.done()) throw std::logic_error("Job is not complete");
        if (handle.promise().error) std::rethrow_exception(handle.promise().error);
        return handle.promise().value;
    }
    const char* stage() const { return handle.promise().state->stage; }
    bool run() { while (!advance()) {} return result(); }
    struct Checkpoint {
        const char* stage;
        bool await_ready() noexcept { return false; }
        void await_suspend(Handle handle) noexcept {
            handle.promise().state->leaf = handle;
            if (stage) handle.promise().state->stage = stage;
        }
        void await_resume() noexcept {}
    };
    static Checkpoint checkpoint(const char* stage = nullptr) { return {stage}; }
    struct Awaiter {
        Handle child;
        bool await_ready() noexcept { return child.done(); }
        std::coroutine_handle<> await_suspend(Handle parent) noexcept {
            child.promise().state = parent.promise().state;
            child.promise().continuation = parent;
            child.promise().state->leaf = child;
            return child;
        }
        bool await_resume() {
            if (child.promise().error) std::rethrow_exception(child.promise().error);
            return child.promise().value;
        }
    };
    Awaiter operator co_await() && noexcept { return {handle}; }
private:
    Handle handle;
};
}
