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
    // The browser cannot pause a WebSocket, so a tab whose game loop is throttled
    // (backgrounded) keeps queueing: allow about eleven minutes of relay bundles
    // (25 per second) within a few megabytes before giving up on the connection.
    static constexpr size_t incomingMessageLimit = 16384;
    static constexpr size_t incomingByteLimit = 4 * queueLimit;
    struct CallbackTarget : std::enable_shared_from_this<CallbackTarget> {
        WebSocketTransport* transport;
        // Copy of the transport's mode, fixed at construction and read on the callback thread.
        const NetMessageMode mode;
        std::atomic<bool> active{true}, overflow{false};
        std::atomic<size_t> queuedBytes{0}, queuedEvents{0};
#ifdef __EMSCRIPTEN_PTHREADS__
        pthread_t owner = pthread_self();
        em_proxying_queue* queue = em_proxying_queue_create();
        ~CallbackTarget() { em_proxying_queue_destroy(queue); }
#endif
        explicit CallbackTarget(WebSocketTransport* transport) : transport(transport), mode(transport->mode) {}
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
        if (count >= incomingMessageLimit || size + bytes > incomingByteLimit) {
            --target->queuedEvents; target->queuedBytes -= bytes;
            target->overflow = true;
            return;
        }
        auto* event = new Event{target, bytes, std::move(function)};
#ifdef __EMSCRIPTEN_PTHREADS__
        if (!emscripten_proxy_async(target->queue, target->owner, apply, event)) {
            target->overflow = true;
            delete event;
        }
#else
        apply(event);
#endif
    }
    EMSCRIPTEN_WEBSOCKET_T socket = 0;
    State status = State::Closed;
    NetMessageMode mode;
    std::deque<std::vector<uint8_t>> incoming;
    size_t incomingBytes = 0;
    std::string failure;
    std::chrono::steady_clock::time_point started;
    bool take(std::vector<uint8_t>& bytes) {
        if (incoming.empty()) return false;
        bytes = std::move(incoming.front()); incoming.pop_front();
        incomingBytes -= bytes.size();
        return true;
    }
public:
    explicit WebSocketTransport(NetMessageMode mode) : mode(mode) {}
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
            auto* target = static_cast<CallbackTarget*>(data);
            const bool text = target->mode == NetMessageMode::Text;
            // Text payloads arrive NUL-terminated; the terminator is not part of the message.
            const size_t size = e->isText && e->numBytes ? e->numBytes - 1 : e->numBytes;
            // A message of the other kind, or an oversized one, closes the connection.
            if (bool(e->isText) != text || size > (text ? textMessageLimit : 64 * 1024)) {
                target->overflow = true;
                return true;
            }
            std::vector<uint8_t> bytes;
            if (size) bytes.assign(e->data, e->data + size);
            post(data, size, [socket=e->socket, text, bytes=std::move(bytes)](auto& self) mutable {
                if (self.socket != socket) return;
                if (bytes.size() > incomingByteLimit - self.incomingBytes || self.incoming.size() >= incomingMessageLimit) {
                    self.failure = "Invalid WebSocket message or input queue overflow"; self.close(); return;
                }
                // An empty text message is still a message; an empty binary one carries nothing.
                if (!bytes.empty() || text) {
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
#ifdef __EMSCRIPTEN_PTHREADS__
        // Delete is a UI barrier. Drain this connection's disabled Events on
        // their owner before it exits, so canceled async tasks cannot leak.
        if (callbacks) emscripten_proxy_execute_queue(callbacks->queue);
#endif
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
        if (mode != NetMessageMode::Binary || status != State::Connected ||
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
    bool sendText(std::string text) override {
        size_t buffered = 0;
        if (mode != NetMessageMode::Text || status != State::Connected || text.size() > textMessageLimit ||
            text.find('\0') != std::string::npos ||
            emscripten_websocket_get_buffered_amount(socket, &buffered) != EMSCRIPTEN_RESULT_SUCCESS ||
            buffered > queueLimit || text.size() > queueLimit - buffered) return false;
        return emscripten_websocket_send_utf8_text(socket, text.c_str()) == EMSCRIPTEN_RESULT_SUCCESS;
    }
    bool receive(std::vector<uint8_t>& bytes) override {
        return mode == NetMessageMode::Binary && take(bytes);
    }
    bool receiveText(std::string& text) override {
        std::vector<uint8_t> message;
        if (mode != NetMessageMode::Text || !take(message)) return false;
        text.assign(message.begin(), message.end());
        return true;
    }
};
}
std::unique_ptr<NetTransport> makeNetTransport(const NetTlsConfig&, NetMessageMode mode) { return std::make_unique<WebSocketTransport>(mode); }

std::unique_ptr<NetTransportListener> makeNetTransportListener(const NetListenConfig&) {
    throw std::runtime_error("Browser LAN hosting is unavailable");
}
