// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetTransport.h"
#include <emscripten/websocket.h>
#include <deque>
#include <cstdlib>

EM_JS(char*, glob2_websocket_endpoint, (int port), {
    const route = port === 7491 ? '/router' : '/yog';
    const base = globalThis.glob2Config?.websocketBase ||
        ((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host);
    return stringToNewUTF8(new URL(route, base).href);
});

namespace {
class WebSocketTransport final : public NetTransport {
    EMSCRIPTEN_WEBSOCKET_T socket = 0;
    State status = State::Closed;
    std::deque<std::vector<uint8_t>> incoming;
    size_t incomingBytes = 0;
public:
    ~WebSocketTransport() override { close(); }
    void open(const std::string& address, uint16_t port) override {
        close();
        if (!emscripten_websocket_is_supported()) return;
        std::string url = address;
        if (!url.starts_with("ws://") && !url.starts_with("wss://")) {
            char* endpoint = glob2_websocket_endpoint(port);
            url = endpoint;
            std::free(endpoint);
        }
        EmscriptenWebSocketCreateAttributes attributes{};
        attributes.url = url.c_str();
        socket = emscripten_websocket_new(&attributes);
        if (socket <= 0) { socket = 0; return; }
        status = State::Connecting;
        emscripten_websocket_set_onopen_callback(socket, this, [](int, const EmscriptenWebSocketOpenEvent* e, void* data) {
            auto& self = *static_cast<WebSocketTransport*>(data);
            if (self.socket == e->socket) self.status = State::Connected;
            return true;
        });
        emscripten_websocket_set_onclose_callback(socket, this, [](int, const EmscriptenWebSocketCloseEvent* e, void* data) {
            auto& self = *static_cast<WebSocketTransport*>(data);
            if (self.socket == e->socket) self.status = State::Closed;
            return true;
        });
        emscripten_websocket_set_onerror_callback(socket, this, [](int, const EmscriptenWebSocketErrorEvent* e, void* data) {
            auto& self = *static_cast<WebSocketTransport*>(data);
            if (self.socket == e->socket) self.status = State::Closed;
            return true;
        });
        emscripten_websocket_set_onmessage_callback(socket, this, [](int, const EmscriptenWebSocketMessageEvent* e, void* data) {
            auto& self = *static_cast<WebSocketTransport*>(data);
            if (self.socket != e->socket) return true;
            if (e->isText || e->numBytes > queueLimit - self.incomingBytes) { self.close(); return true; }
            if (e->numBytes) {
                self.incoming.emplace_back(e->data, e->data + e->numBytes);
                self.incomingBytes += e->numBytes;
            }
            return true;
        });
    }
    void close() override {
        if (socket) {
            // Deleting the Emscripten handle detaches all four JS callbacks.
            emscripten_websocket_close(socket, 1000, nullptr);
            emscripten_websocket_delete(socket);
            socket = 0;
        }
        status = State::Closed;
        incoming.clear(); incomingBytes = 0;
    }
    State state() const override { return status; }
    bool send(std::vector<uint8_t> bytes) override {
        size_t buffered = 0;
        if (status != State::Connected ||
            emscripten_websocket_get_buffered_amount(socket, &buffered) != EMSCRIPTEN_RESULT_SUCCESS ||
            buffered > queueLimit || bytes.size() > queueLimit - buffered) return false;
        // Gateway messages are limited to 64 KiB. Protocol frames can cross
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
std::unique_ptr<NetTransport> makeNetTransport() { return std::make_unique<WebSocketTransport>(); }
