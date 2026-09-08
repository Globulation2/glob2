// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NetTransport.h"
#include <queue>

using std::shared_ptr;

class NetListener;
class NetMessage;

// Shared message framing/decoding, independent of the platform transport.
class NetConnection {
public:
    NetConnection(const std::string& address, Uint16 port);
    NetConnection();
    explicit NetConnection(std::unique_ptr<NetTransport> transport);
    ~NetConnection();
    void openConnection(const std::string& address, Uint16 port);
    void closeConnection();
    bool isConnected();
    bool isConnecting();
    void update();
    std::shared_ptr<NetMessage> getMessage();
    void sendMessage(std::shared_ptr<NetMessage> message);
    const std::string& getIPAddress() const;
protected:
    friend class NetListener;
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
