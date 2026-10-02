#include <cassert>
#include <emscripten.h>
#include <emscripten/proxying.h>
#include <pthread.h>
#include "net-baseline.inc"
NetEndpoint NetEndpoint::parse(const std::string&) { return {}; }
extern "C" bool emscripten_websocket_is_supported() { return true; }
extern "C" EMSCRIPTEN_WEBSOCKET_T emscripten_websocket_new(EmscriptenWebSocketCreateAttributes*) { static int next=0; return ++next; }
extern "C" EMSCRIPTEN_RESULT emscripten_websocket_close(int, unsigned short, const char*) { return EMSCRIPTEN_RESULT_SUCCESS; }
extern "C" EMSCRIPTEN_RESULT emscripten_websocket_delete(int) { return EMSCRIPTEN_RESULT_SUCCESS; }
extern "C" EMSCRIPTEN_RESULT emscripten_websocket_set_onopen_callback_on_thread(int, void*, em_websocket_open_callback_func, pthread_t) { return EMSCRIPTEN_RESULT_SUCCESS; }
extern "C" EMSCRIPTEN_RESULT emscripten_websocket_set_onclose_callback_on_thread(int, void*, em_websocket_close_callback_func, pthread_t) { return EMSCRIPTEN_RESULT_SUCCESS; }
extern "C" EMSCRIPTEN_RESULT emscripten_websocket_set_onerror_callback_on_thread(int, void*, em_websocket_error_callback_func, pthread_t) { return EMSCRIPTEN_RESULT_SUCCESS; }
extern "C" EMSCRIPTEN_RESULT emscripten_websocket_set_onmessage_callback_on_thread(int, void*, em_websocket_message_callback_func, pthread_t) { return EMSCRIPTEN_RESULT_SUCCESS; }
extern "C" EMSCRIPTEN_RESULT emscripten_websocket_get_buffered_amount(int, size_t*) { return EMSCRIPTEN_RESULT_SUCCESS; }
extern "C" EMSCRIPTEN_RESULT emscripten_websocket_send_binary(int, void*, uint32_t) { return EMSCRIPTEN_RESULT_SUCCESS; }
struct Enqueue { void* target; std::shared_ptr<int>* sentinel; };
void enqueue(void* opaque) {
    auto& e = *static_cast<Enqueue*>(opaque);
    WebSocketTransport::post(e.target, 32, [sentinel=*e.sentinel](auto&) { assert(false && "closed callback executed"); });
}
auto* ui = em_proxying_queue_create();
void* test(void*) {
    WebSocketTransport a, b;
    a.open("wss://test", 0); b.open("wss://test", 0);
    auto sentinelA = std::make_shared<int>(1), sentinelB = std::make_shared<int>(2);
    std::weak_ptr<int> weakA=sentinelA, weakB=sentinelB;
    Enqueue ea{a.callbacks.get(), &sentinelA}, eb{b.callbacks.get(), &sentinelB};
    assert(emscripten_proxy_sync(ui, emscripten_main_runtime_thread_id(), enqueue, &ea));
    assert(emscripten_proxy_sync(ui, emscripten_main_runtime_thread_id(), enqueue, &eb));
    assert(a.callbacks->queuedEvents == 1 && b.callbacks->queuedEvents == 1);
    std::weak_ptr<WebSocketTransport::CallbackTarget> targetA=a.callbacks, targetB=b.callbacks;
    sentinelA.reset(); sentinelB.reset();
    a.close();
    assert(weakA.expired() && targetA.expired());
    assert(!weakB.expired() && !targetB.expired()); // don't run another connection's events
    b.close();
    assert(weakB.expired() && targetB.expired());
    MAIN_THREAD_EM_ASM({ window.result=0; console.log('PASS pending WebSocket events reclaimed independently before pthread exit'); });
    return nullptr;
}
int main() {
    pthread_t owner;
    assert(pthread_create(&owner, nullptr, test, nullptr) == 0);
    emscripten_exit_with_live_runtime();
}
