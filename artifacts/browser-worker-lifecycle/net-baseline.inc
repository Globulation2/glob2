// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetTransport.h"
#include <emscripten/websocket.h>
#include <emscripten.h>
#include <emscripten/threading.h>
#include <deque>
#include <cstdlib>
#include <stdexcept>
#include <chrono>
#include <atomic>
#include <functional>
#include <emscripten/proxying.h>


namespace {
class WebSocketTransport final : public NetTransport {
public: // test access only
    struct CallbackTarget : std::enable_shared_from_this<CallbackTarget> {
        WebSocketTransport* transport;
        std::atomic<bool> active{true}, overflow{false};
        std::atomic<size_t> queuedBytes{0}, queuedEvents{0};
#ifdef __EMSCRIPTEN_PTHREADS__
        pthread_t owner = pthread_self();
#endif
        explicit CallbackTarget(WebSocketTransport* transport) : transport(transport) {}
    };
    std::shared_ptr<CallbackTarget> callbacks;
    struct Event {
        std::shared_ptr<CallbackTarget> target;
        size_t bytes;
        std::function<void(WebSocketTransport&)> apply;
        ~Event() { target->queuedBytes -= bytes; --target->queuedEvents; }
    };
    static void apply(void* opaque) {
        std::unique_ptr<Event> event(static_cast<Event*>(opaque));
        if (event->target->active) event->apply(*event->target->transport);
    }
    static void post(void* opaque, size_t bytes, std::function<void(WebSocketTransport&)> function) {
        auto target = static_cast<CallbackTarget*>(opaque)->shared_from_this();
        if (!target->active) return;
        const auto count = target->queuedEvents.fetch_add(1);
        const auto size = target->queuedBytes.fetch_add(bytes);
        if (count >= 256 || size + bytes > queueLimit) {
            --target->queuedEvents; target->queuedBytes -= bytes;
            target->overflow = true;
            return;
        }
        auto* event = new Event{target, bytes, std::move(function)};
#ifdef __EMSCRIPTEN_PTHREADS__
        static auto* queue = em_proxying_queue_create();
        if (!emscripten_proxy_async(queue, target->owner, apply, event)) {
            target->overflow = true;
            delete event;
        }
#else
        apply(event);
#endif
    }
    EMSCRIPTEN_WEBSOCKET_T socket = 0;
    State status = State::Closed;
    std::deque<std::vector<uint8_t>> incoming;
    size_t incomingBytes = 0;
    std::string failure;
    std::chrono::steady_clock::time_point started;
public:
    ~WebSocketTransport() override { close(); }
    void open(const std::string& address, uint16_t port) override {
        close();
        failure.clear();
        if (!emscripten_websocket_is_supported()) { failure = "WebSockets are unavailable in this browser"; return; }
        std::string url = address;
        try { NetEndpoint::parse(url); } catch (const std::exception& error) { failure = error.what(); return; }
        if (url.find('#') != std::string::npos) { failure = "Browser LAN joining requires a certificate trusted by the browser/device"; return; }
        EmscriptenWebSocketCreateAttributes attributes{};
        attributes.url = url.c_str();
        socket = emscripten_websocket_new(&attributes);
        if (socket <= 0) { socket = 0; failure = "WebSocket creation failed"; return; }
        status = State::Connecting;
        started = std::chrono::steady_clock::now();
        callbacks = std::make_shared<CallbackTarget>(this);
        // The pinned SDK invokes callbacks on the UI even when _on_thread is
        // requested. Copy transient event bytes there, then apply on the owner.
        emscripten_websocket_set_onopen_callback(socket, callbacks.get(), [](int, const EmscriptenWebSocketOpenEvent* e, void* data) {
            post(data, 0, [socket=e->socket](auto& self) {
                if (self.socket == socket) self.status = State::Connected;
            });
            return true;
        });
        emscripten_websocket_set_onclose_callback(socket, callbacks.get(), [](int, const EmscriptenWebSocketCloseEvent* e, void* data) {
            post(data, 0, [socket=e->socket](auto& self) {
                if (self.socket == socket) {
                    self.status = State::Closed;
                    if (self.failure.empty()) self.failure = "Secure WebSocket connection closed";
                }
            });
            return true;
        });
        emscripten_websocket_set_onerror_callback(socket, callbacks.get(), [](int, const EmscriptenWebSocketErrorEvent* e, void* data) {
            post(data, 0, [socket=e->socket](auto& self) {
                if (self.socket == socket) {
                    self.status = State::Closed;
                    self.failure = "Secure WebSocket connection failed; check endpoint, certificate trust, and local network permission";
                }
            });
            return true;
        });
        emscripten_websocket_set_onmessage_callback(socket, callbacks.get(), [](int, const EmscriptenWebSocketMessageEvent* e, void* data) {
            if (e->isText || e->numBytes > 64 * 1024) {
                static_cast<CallbackTarget*>(data)->overflow = true;
                return true;
            }
            std::vector<uint8_t> bytes;
            if (e->numBytes) bytes.assign(e->data, e->data + e->numBytes);
            post(data, e->numBytes, [socket=e->socket, bytes=std::move(bytes)](auto& self) mutable {
                if (self.socket != socket) return;
                if (bytes.size() > queueLimit - self.incomingBytes || self.incoming.size() >= 256) {
                    self.failure = "Invalid WebSocket message or input queue overflow"; self.close(); return;
                }
                if (!bytes.empty()) {
                    self.incomingBytes += bytes.size();
                    self.incoming.push_back(std::move(bytes));
                }
            });
            return true;
        });
    }
    void close() override {
        if (callbacks) callbacks->active = false;
        if (socket) {
            // The pinned SDK deletes synchronously on the UI event loop. Earlier
            // callbacks finish before it returns; later events see null handlers.
            // Retain userdata until that barrier, then invalidate queued Events.
            emscripten_websocket_close(socket, 1000, nullptr);
            emscripten_websocket_delete(socket);
            socket = 0;
        }
        callbacks.reset();
        status = State::Closed;
        incoming.clear(); incomingBytes = 0;
    }
    State state() const override {
        if (callbacks && callbacks->overflow) {
            auto& self = *const_cast<WebSocketTransport*>(this);
            self.failure = "Invalid WebSocket message or callback queue overflow";
            self.close();
        }
        if (status == State::Connecting && std::chrono::steady_clock::now() - started >= std::chrono::seconds(10)) {
            auto& self = *const_cast<WebSocketTransport*>(this);
            self.failure = "Connection handshake timed out"; self.close();
        }
        return status;
    }
    std::string error() const override { return failure; }
    bool send(std::vector<uint8_t> bytes) override {
        size_t buffered = 0;
        if (status != State::Connected ||
            emscripten_websocket_get_buffered_amount(socket, &buffered) != EMSCRIPTEN_RESULT_SUCCESS ||
            buffered > queueLimit || bytes.size() > queueLimit - buffered) return false;
        // WebSocket messages are limited to 64 KiB. Protocol frames can cross
        // any number of WebSocket messages, just as they cross TCP reads.
        for (size_t offset = 0; offset < bytes.size(); offset += chunkLimit) {
            const size_t count = std::min(chunkLimit, bytes.size() - offset);
            if (emscripten_websocket_send_binary(socket, bytes.data() + offset, count) != EMSCRIPTEN_RESULT_SUCCESS) return false;
        }
        return true;
    }
    bool receive(std::vector<uint8_t>& bytes) override {
        if (incoming.empty()) return false;
        bytes = std::move(incoming.front()); incoming.pop_front();
        incomingBytes -= bytes.size();
        return true;
    }
};
}
std::unique_ptr<NetTransport> makeNetTransport(const NetTlsConfig&) { return std::make_unique<WebSocketTransport>(); }

std::unique_ptr<NetTransportListener> makeNetTransportListener(const NetListenConfig&) {
    throw std::runtime_error("Browser LAN hosting is unavailable");
}

#include "ServerControl.h"
struct ServerControl::Impl {};
ServerControl::ServerControl(const std::string&, unsigned short) { throw std::runtime_error("Native server required"); }
ServerControl::~ServerControl() = default;
void ServerControl::update(const ServerStatus&) {}
void ServerControl::installSignals() {}
bool ServerControl::shutdownRequested() { return false; }
struct ServerDataLock::Impl {};
ServerDataLock::ServerDataLock(const std::string&) { throw std::runtime_error("Native server required"); }
ServerDataLock::~ServerDataLock() = default;

std::string configuredYogEndpoint(const std::string&) {
    char* value = reinterpret_cast<char*>(MAIN_THREAD_EM_ASM_PTR({
        return stringToNewUTF8(globalThis.glob2Config?.yogEndpoint || ('wss://' + location.host + '/yog'));
    })); std::string result(value); std::free(value); return result;
}
