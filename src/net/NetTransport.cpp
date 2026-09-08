// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetTransport.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <array>

namespace {
class TcpTransport final : public NetTransport {
    std::atomic<State> status{State::Closed};
    std::atomic<bool> stop{false};
    std::thread worker;
    std::mutex mutex;
    std::deque<std::vector<uint8_t>> incoming, outgoing;
    size_t incomingBytes = 0, outgoingBytes = 0;

    void run(TCPsocket socket, std::string host, uint16_t port) {
        if (!socket) {
            IPaddress address{};
            if (SDLNet_ResolveHost(&address, host.c_str(), port) == 0 && !stop)
                socket = SDLNet_TCP_Open(&address);
        }
        auto set = SDLNet_AllocSocketSet(1);
        if (socket && set && SDLNet_TCP_AddSocket(set, socket) >= 0 && !stop) {
            status = State::Connected;
            while (!stop) {
                std::vector<uint8_t> bytes;
                {
                    std::lock_guard lock(mutex);
                    if (!outgoing.empty()) {
                        bytes = std::move(outgoing.front());
                        outgoing.pop_front();
                        outgoingBytes -= bytes.size();
                    }
                }
                if (!bytes.empty() && SDLNet_TCP_Send(socket, bytes.data(), bytes.size()) != int(bytes.size())) break;
                const int ready = SDLNet_CheckSockets(set, 10);
                if (ready < 0) break;
                if (ready && SDLNet_SocketReady(socket)) {
                    std::array<uint8_t, chunkLimit> buffer;
                    const int size = SDLNet_TCP_Recv(socket, buffer.data(), buffer.size());
                    if (size <= 0) break;
                    std::lock_guard lock(mutex);
                    if (incomingBytes + size > queueLimit) break;
                    incoming.emplace_back(buffer.begin(), buffer.begin() + size);
                    incomingBytes += size;
                }
            }
        }
        if (socket) SDLNet_TCP_Close(socket);
        if (set) SDLNet_FreeSocketSet(set);
        status = State::Closed;
    }
public:
    ~TcpTransport() override { close(); }
    void open(const std::string& host, uint16_t port) override {
        close();
        stop = false;
        status = State::Connecting;
        worker = std::thread([this, host, port] { run(nullptr, host, port); });
    }
    bool accept(TCPsocket socket) override {
        close();
        stop = false;
        // The accepted socket is already connected; callers can immediately
        // queue the server greeting while the worker takes ownership.
        status = State::Connected;
        worker = std::thread([this, socket] { run(socket, {}, 0); });
        return true;
    }
    void close() override {
        stop = true;
        if (worker.joinable()) worker.join();
        status = State::Closed;
        std::lock_guard lock(mutex);
        incoming.clear(); outgoing.clear();
        incomingBytes = outgoingBytes = 0;
    }
    State state() const override { return status; }
    bool send(std::vector<uint8_t> bytes) override {
        std::lock_guard lock(mutex);
        if (status != State::Connected || bytes.size() > queueLimit - outgoingBytes) return false;
        outgoingBytes += bytes.size();
        outgoing.push_back(std::move(bytes));
        return true;
    }
    bool receive(std::vector<uint8_t>& bytes) override {
        std::lock_guard lock(mutex);
        if (incoming.empty()) return false;
        bytes = std::move(incoming.front()); incoming.pop_front();
        incomingBytes -= bytes.size();
        return true;
    }
};
}
std::unique_ptr<NetTransport> makeNetTransport() { return std::make_unique<TcpTransport>(); }
