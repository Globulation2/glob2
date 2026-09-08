// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "NetConnection.h"
#include "NetMessage.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <stdexcept>

namespace {
// Legacy MemoryStreamBackend supplies zeros on overread. Network payloads must
// fail closed instead of allowing truncated messages to acquire default fields.
class PacketInput final : public GAGCore::MemoryStreamBackend {
    size_t length;
public:
    PacketInput(const void* bytes, size_t size) : MemoryStreamBackend(bytes, size), length(size) { seekFromStart(0); }
    void read(void* bytes, size_t size) override {
        if (size > length - getPosition()) throw std::runtime_error("Truncated network message");
        MemoryStreamBackend::read(bytes, size);
    }
};
}
NetConnection::NetConnection() : NetConnection(makeNetTransport()) {}
NetConnection::NetConnection(std::unique_ptr<NetTransport> selected) : transport(std::move(selected)) {
    if (!transport) throw std::invalid_argument("Network transport is required");
}
NetConnection::NetConnection(const std::string& host, Uint16 port) : NetConnection() { openConnection(host, port); }
NetConnection::~NetConnection() = default;
void NetConnection::openConnection(const std::string& host, Uint16 port) {
    closeConnection();
    address = host;
    transport->open(host, port);
}
void NetConnection::closeConnection() {
    transport->close();
    pending.clear();
    received = {};
    outgoing = {}; outgoingBytes = 0;
}
bool NetConnection::isConnected() { return transport->state() == NetTransport::State::Connected; }
bool NetConnection::isConnecting() { return transport->state() == NetTransport::State::Connecting; }
void NetConnection::flushOutgoing() {
    while (isConnected() && !outgoing.empty()) {
        auto bytes = std::move(outgoing.front()); outgoing.pop();
        outgoingBytes -= bytes.size();
        if (!transport->send(std::move(bytes))) { closeConnection(); break; }
    }
}
void NetConnection::update() {
    flushOutgoing();
    std::vector<uint8_t> bytes;
    try {
        while (transport->receive(bytes)) {
            if (bytes.size() > NetTransport::queueLimit - pending.size()) throw std::runtime_error("Network input overflow");
            pending.insert(pending.end(), bytes.begin(), bytes.end());
            size_t offset = 0;
            while (pending.size() - offset >= 2) {
                const size_t length = (size_t(pending[offset]) << 8) | pending[offset + 1];
                if (!length) throw std::runtime_error("Empty network frame");
                if (pending.size() - offset - 2 < length) break;
                auto* backend = new PacketInput(pending.data() + offset + 2, length);
                GAGCore::BinaryInputStream stream(backend);
                auto message = NetMessage::getNetMessage(&stream);
                if (!message || backend->getPosition() != length || received.size() >= 256)
                    throw std::runtime_error("Invalid network message or queue overflow");
                received.push(std::move(message));
                offset += length + 2;
            }
            pending.erase(pending.begin(), pending.begin() + offset);
        }
    } catch (const std::exception&) {
        closeConnection();
    }
}
std::shared_ptr<NetMessage> NetConnection::getMessage() {
    update();
    if (received.empty()) return {};
    auto message = std::move(received.front()); received.pop();
    return message;
}
void NetConnection::sendMessage(std::shared_ptr<NetMessage> message) {
    if (!message || (!isConnected() && !isConnecting())) return;
    auto* backend = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream stream(backend);
    stream.writeUint8(message->getMessageType(), "messageType");
    message->encodeData(&stream);
    const size_t length = backend->getPosition();
    if (!length || length > 65535) { closeConnection(); return; }
    std::vector<uint8_t> bytes(length + 2);
    bytes[0] = length >> 8; bytes[1] = length & 255;
    backend->seekFromStart(0);
    backend->read(bytes.data() + 2, length);
    if (bytes.size() > NetTransport::queueLimit - outgoingBytes) { closeConnection(); return; }
    outgoingBytes += bytes.size();
    outgoing.push(std::move(bytes));
    flushOutgoing();
}
const std::string& NetConnection::getIPAddress() const { return address; }
bool NetConnection::attemptConnection(TCPsocket& listener) {
    TCPsocket socket = SDLNet_TCP_Accept(listener);
    if (!socket) return false;
    const auto* peer = SDLNet_TCP_GetPeerAddress(socket);
    const auto* ip = reinterpret_cast<const unsigned char*>(&peer->host);
    address = std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." + std::to_string(ip[2]) + "." + std::to_string(ip[3]);
    closeConnection();
    if (transport->accept(socket)) return true;
    SDLNet_TCP_Close(socket);
    return false;
}
