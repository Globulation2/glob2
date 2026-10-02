// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetTransport.h"
#include <emscripten/websocket.h>
#include <emscripten.h>
#include <deque>
#include <cstdlib>
#include <stdexcept>
#include <chrono>


namespace {
class WebSocketTransport final : public NetTransport {
    static constexpr size_t incomingMessageLimit = 16384;
    static constexpr size_t incomingByteLimit = 4 * queueLimit;
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
        emscripten_websocket_set_onopen_callback(socket, this, [](int, const EmscriptenWebSocketOpenEvent* e, void* data) {
            auto& self = *static_cast<WebSocketTransport*>(data);
            if (self.socket == e->socket) self.status = State::Connected;
            return true;
        });
        emscripten_websocket_set_onclose_callback(socket, this, [](int, const EmscriptenWebSocketCloseEvent* e, void* data) {
            auto& self = *static_cast<WebSocketTransport*>(data);
            if (self.socket == e->socket) { self.status = State::Closed; if (self.failure.empty()) self.failure = "Secure WebSocket connection closed"; }
            return true;
        });
        emscripten_websocket_set_onerror_callback(socket, this, [](int, const EmscriptenWebSocketErrorEvent* e, void* data) {
            auto& self = *static_cast<WebSocketTransport*>(data);
            if (self.socket == e->socket) { self.status = State::Closed; self.failure = "Secure WebSocket connection failed; check endpoint, certificate trust, and local network permission"; }
            return true;
        });
        emscripten_websocket_set_onmessage_callback(socket, this, [](int, const EmscriptenWebSocketMessageEvent* e, void* data) {
            auto& self = *static_cast<WebSocketTransport*>(data);
            if (self.socket != e->socket) return true;
            const bool text = self.mode == NetMessageMode::Text;
            // Text payloads arrive NUL-terminated; the terminator is not part of the message.
            const size_t size = e->isText && e->numBytes ? e->numBytes - 1 : e->numBytes;
            // The browser cannot pause a WebSocket, so a tab whose game loop is throttled
            // (backgrounded) keeps queueing: allow about eleven minutes of relay bundles
            // (25 per second) within a few megabytes before giving up on the connection.
            if (bool(e->isText) != text || size > (text ? textMessageLimit : 64 * 1024) ||
                size > incomingByteLimit - self.incomingBytes || self.incoming.size() >= incomingMessageLimit) {
                self.failure = "Invalid WebSocket message or input queue overflow"; self.close(); return true;
            }
            if (size || text) {
                self.incoming.emplace_back(e->data, e->data + size);
                self.incomingBytes += size;
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
    State state() const override {
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

EM_JS(char*, browserLobbyEndpoint, (), {
    return stringToNewUTF8(globalThis.glob2Config?.yogEndpoint || ('wss://' + location.host + '/yog'));
});
std::string configuredYogEndpoint(const std::string&) {
    char* value = browserLobbyEndpoint(); std::string result(value); std::free(value); return result;
}
