// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL_net.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Byte-stream transport. Message framing and decoding belong to NetConnection.
class NetTransport {
public:
    enum class State { Closed, Connecting, Connected };
    static constexpr size_t queueLimit = 1024 * 1024;
    static constexpr size_t chunkLimit = 16 * 1024;
    virtual ~NetTransport() = default;
    virtual void open(const std::string& address, uint16_t port) = 0;
    virtual void close() = 0;
    virtual State state() const = 0;
    virtual bool send(std::vector<uint8_t> bytes) = 0;
    virtual bool receive(std::vector<uint8_t>& bytes) = 0;
    // Ownership transfers only on success. Only native TCP supports accepting.
    virtual bool accept(TCPsocket socket) { return false; }
};
std::unique_ptr<NetTransport> makeNetTransport();
