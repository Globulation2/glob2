// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetTransport.h"
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <array>

#if !defined(YOG_SERVER_ONLY) && defined(GLOB2_NATIVE_WSS)
std::unique_ptr<NetTransport> makeWssTransport();
#endif

namespace
{
class TcpTransport final : public NetTransport
{
	std::atomic<State> status{State::Closed};
	std::atomic<bool> stop{false};
	std::thread worker;
	std::mutex mutex;
	std::deque<std::vector<uint8_t>> incoming, outgoing;
	size_t incomingBytes = 0, outgoingBytes = 0;

	void run(NET_StreamSocket *socket, std::string host, uint16_t port)
	{

        if (!socket)
        {
            NET_Address *address = NET_ResolveHostname(host.c_str());
            if (address) {
                while (!stop && NET_GetAddressStatus(address) == NET_WAITING)
                    NET_WaitUntilResolved(address, 10);
                if (!stop && NET_GetAddressStatus(address) == NET_SUCCESS)
                    socket = NET_CreateClient(address, port, 0);
                NET_UnrefAddress(address);
            }
        }
        if (socket) {
            while (!stop && NET_GetConnectionStatus(socket) == NET_WAITING)
                NET_WaitUntilConnected(socket, 10);
        }
        if (socket && !stop && NET_GetConnectionStatus(socket) == NET_SUCCESS)
        {
            status = State::Connected;
            while (!stop)
            {
                bool wrote = false;
                {
                    std::lock_guard lock(mutex);
                    const int pending = NET_GetStreamSocketPendingWrites(socket);
                    if (pending < 0) break;
                    // outgoingBytes includes both our queue and SDL_net's queue.
                    size_t queued = 0;
                    for (const auto &bytes : outgoing) queued += bytes.size();
                    outgoingBytes = queued + static_cast<size_t>(pending);
                    if (!outgoing.empty()) {
                        auto &bytes = outgoing.front();
                        if (!NET_WriteToStreamSocket(socket, bytes.data(), static_cast<int>(bytes.size()))) break;
                        outgoing.pop_front();
                        wrote = true;
                    }
                }
                std::array<uint8_t, chunkLimit> buffer;
                const int size = NET_ReadFromStreamSocket(socket, buffer.data(), buffer.size());
                if (size < 0) break;
                if (size) {
                    std::lock_guard lock(mutex);
                    if (incomingBytes + size > queueLimit) break;
                    incoming.emplace_back(buffer.begin(), buffer.begin() + size);
                    incomingBytes += size;
                } else if (!wrote) {
                    void *sockets[] = {socket};
                    if (NET_WaitUntilInputAvailable(sockets, 1, 10) < 0) break;
                }
            }
        }
        if (socket) NET_DestroyStreamSocket(socket);
        status = State::Closed;
    }

  public:
	~TcpTransport() override { close(); }
	void open(const std::string &host, uint16_t port) override
	{
		close();
		stop = false;
		status = State::Connecting;
		worker = std::thread([this, host, port] { run(nullptr, host, port); });
	}
	bool accept(NET_StreamSocket *socket) override
	{
		close();
		stop = false;
		// The accepted socket is already connected; callers can immediately
		// queue the server greeting while the worker takes ownership.
		status = State::Connected;
		worker = std::thread([this, socket] { run(socket, {}, 0); });
		return true;
	}
	void close() override
	{
		stop = true;
		if (worker.joinable())
			worker.join();
		status = State::Closed;
		std::lock_guard lock(mutex);
		incoming.clear();
		outgoing.clear();
		incomingBytes = outgoingBytes = 0;
	}
	State state() const override { return status; }
	bool send(std::vector<uint8_t> bytes) override
	{
		std::lock_guard lock(mutex);
		if (status != State::Connected || bytes.size() > queueLimit - outgoingBytes)
			return false;
		outgoingBytes += bytes.size();
		outgoing.push_back(std::move(bytes));
		return true;
	}
	bool receive(std::vector<uint8_t> &bytes) override
	{
		std::lock_guard lock(mutex);
		if (incoming.empty())
			return false;
		bytes = std::move(incoming.front());
		incoming.pop_front();
		incomingBytes -= bytes.size();
		return true;
	}
};
} // namespace
#ifndef YOG_SERVER_ONLY
namespace
{
class NativeTransport final : public NetTransport
{
	std::unique_ptr<NetTransport> selected;

  public:
	void open(const std::string &address, uint16_t port) override
	{
		close();
		if (address.rfind("wss://", 0) == 0)
		{
#ifdef GLOB2_NATIVE_WSS
			selected = makeWssTransport();
#else
			return;
#endif
		}
		else
		{
			selected = std::make_unique<TcpTransport>();
		}
		selected->open(address, port);
	}
	void close() override { selected.reset(); }
	State state() const override { return selected ? selected->state() : State::Closed; }
	bool send(std::vector<uint8_t> bytes) override
	{
		return selected && selected->send(std::move(bytes));
	}
	bool receive(std::vector<uint8_t> &bytes) override
	{
		return selected && selected->receive(bytes);
	}
	bool accept(NET_StreamSocket *socket) override
	{
		close();
		selected = std::make_unique<TcpTransport>();
		return selected->accept(socket);
	}
};
} // namespace
#endif
std::unique_ptr<NetTransport> makeNetTransport()
{
#ifdef YOG_SERVER_ONLY
	return std::make_unique<TcpTransport>();
#else
	return std::make_unique<NativeTransport>();
#endif
}
