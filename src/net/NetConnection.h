// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once
#include "NetTransport.h"
#include <queue>

using std::shared_ptr;

class NetListener;
class NetMessage;

/// Owns platform transport while keeping message framing and decoding shared.
class NetConnection {
public:
    /// Starts connecting to the given address and port.
    NetConnection(const std::string& address, Uint16 port);
    /// Creates a disconnected connection with the default platform transport.
    NetConnection();
    /// Creates a connection with an injected transport.
    explicit NetConnection(std::unique_ptr<NetTransport> transport);
    /// Closes the transport and releases queued messages.
    ~NetConnection();
    /// Replaces any current connection and starts connecting.
    void openConnection(const std::string& address, Uint16 port);
    /// Closes the current connection and clears its queues.
    void closeConnection();
    /// Returns whether the transport is connected.
    bool isConnected();
    /// Returns whether the transport is still connecting.
    bool isConnecting();
    /// Transfers available transport bytes into complete messages.
    void update();
    /// Returns the next received message, or an empty pointer when none is ready.
    std::shared_ptr<NetMessage> getMessage();
    /// Queues one framed message for ordered delivery.
    void sendMessage(std::shared_ptr<NetMessage> message);
    /// Returns the configured peer address.
    const std::string& getIPAddress() const;
protected:
    friend class NetListener;
    /// Accepts one connection from the native SDL listener when available.
    bool attemptConnection(TCPsocket& serverSocket);
private:
    std::unique_ptr<NetTransport> transport;
    std::queue<std::shared_ptr<NetMessage>> received;
    std::vector<uint8_t> pending;
    std::string address;
    std::queue<std::vector<uint8_t>> outgoing;
    size_t outgoingBytes = 0;
    void flushOutgoing();
};
