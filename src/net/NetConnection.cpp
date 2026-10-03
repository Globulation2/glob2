// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "NetConnection.h"
#include "NetMessage.h"
#include <BinaryStream.h>
#include "PacketInput.h"
#include <stdexcept>

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
    reader.clear();
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
            if (!reader.append(bytes.data(), bytes.size(), NetTransport::queueLimit))
                throw std::runtime_error("Network input overflow");
            const uint8_t* data = nullptr;
            size_t length = 0;
            while (reader.next(data, length)) {
                if (!length) throw std::runtime_error("Empty network frame");
                auto* backend = new PacketInput(data, length);
                GAGCore::BinaryInputStream stream(backend);
                auto message = NetMessage::getNetMessage(&stream);
                if (!message || backend->getPosition() != length || received.size() >= 256)
                    throw std::runtime_error("Invalid network message or queue overflow");
                received.push(std::move(message));
            }
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
    std::vector<uint8_t> payload(length);
    backend->seekFromStart(0);
    backend->read(payload.data(), length);
    auto bytes = NetFrame::encode(payload);
    if (bytes.size() > NetTransport::queueLimit - outgoingBytes) { closeConnection(); return; }
    outgoingBytes += bytes.size();
    outgoing.push(std::move(bytes));
    flushOutgoing();
}
const std::string& NetConnection::getIPAddress() const { return address; }
