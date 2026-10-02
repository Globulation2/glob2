// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetTransport.h"
#include <SDL3_net/SDL_net.h>
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace
{
class TcpTransport final : public NetTransport
{
	std::atomic<State> status{State::Closed};
	std::atomic<bool> stop{false};
	std::thread worker;
	mutable std::mutex mutex;
	std::deque<std::vector<uint8_t>> incoming, outgoing;
	size_t incomingBytes = 0, outgoingBytes = 0;
	std::string peer;

	void setPeer(NET_StreamSocket *socket)
	{
		NET_Address *remote = NET_GetStreamSocketAddress(socket);
		const char *text = remote ? NET_GetAddressString(remote) : nullptr;
		std::lock_guard lock(mutex);
		peer = text ? text : "";
		if (peer.rfind("::ffff:", 0) == 0 && peer.find('.', 7) != std::string::npos)
			peer.erase(0, 7);
		if (remote)
			NET_UnrefAddress(remote);
	}

	void run(NET_StreamSocket *socket, std::string host, uint16_t port)
	{
		if (!socket)
		{
			NET_Address *address = NET_ResolveHostname(host.c_str());
			if (address)
			{
				while (!stop && NET_GetAddressStatus(address) == NET_WAITING)
					NET_WaitUntilResolved(address, 10);
				if (!stop && NET_GetAddressStatus(address) == NET_SUCCESS)
					socket = NET_CreateClient(address, port, 0);
				NET_UnrefAddress(address);
			}
		}
		if (socket)
		{
			while (!stop && NET_GetConnectionStatus(socket) == NET_WAITING)
				NET_WaitUntilConnected(socket, 10);
		}
		if (socket && !stop && NET_GetConnectionStatus(socket) == NET_SUCCESS)
		{
			setPeer(socket);
			status = State::Connected;
			while (!stop)
			{
				bool wrote = false;
				{
					std::lock_guard lock(mutex);
					const int pending = NET_GetStreamSocketPendingWrites(socket);
					if (pending < 0)
						break;
					// outgoingBytes includes both our queue and SDL_net's queue.
					size_t queued = 0;
					for (const auto &bytes : outgoing)
						queued += bytes.size();
					outgoingBytes = queued + static_cast<size_t>(pending);
					if (!outgoing.empty())
					{
						auto &bytes = outgoing.front();
						if (!NET_WriteToStreamSocket(socket, bytes.data(),
													 static_cast<int>(bytes.size())))
							break;
						outgoing.pop_front();
						wrote = true;
					}
				}
				std::array<uint8_t, chunkLimit> buffer;
				const int size = NET_ReadFromStreamSocket(socket, buffer.data(), buffer.size());
				if (size < 0)
					break;
				if (size)
				{
					std::lock_guard lock(mutex);
					if (incomingBytes + size > queueLimit)
						break;
					incoming.emplace_back(buffer.begin(), buffer.begin() + size);
					incomingBytes += size;
				}
				else if (!wrote)
				{
					void *sockets[] = {socket};
					if (NET_WaitUntilInputAvailable(sockets, 1, 10) < 0)
						break;
				}
			}
		}
		if (socket)
			NET_DestroyStreamSocket(socket);
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
	bool accept(NET_StreamSocket *socket)
	{
		close();
		stop = false;
		setPeer(socket);
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
		peer.clear();
	}
	State state() const override { return status; }
	std::string peerAddress() const override
	{
		std::lock_guard lock(mutex);
		return peer;
	}
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

class TcpListener final : public NetTransportListener
{
	NET_Server *server = nullptr;

  public:
	explicit TcpListener(const NetListenConfig &config)
	{
		NET_Address *address = nullptr;
		const bool wildcard = config.bindAddress.empty() || config.bindAddress == "0.0.0.0" ||
							  config.bindAddress == "::";
		if (!wildcard)
		{
			address = NET_ResolveHostname(config.bindAddress.c_str());
			if (!address || NET_WaitUntilResolved(address, 1000) != NET_SUCCESS)
			{
				if (address)
					NET_UnrefAddress(address);
				return;
			}
		}
		server = NET_CreateServer(address, config.port, 0);
		if (address)
			NET_UnrefAddress(address);
	}
	~TcpListener() override { close(); }
	std::unique_ptr<NetTransport> accept() override
	{
		NET_StreamSocket *socket = nullptr;
		if (!server || !NET_AcceptClient(server, &socket) || !socket)
			return {};
		auto transport = std::make_unique<TcpTransport>();
		try
		{
			transport->accept(socket);
		}
		catch (...)
		{
			NET_DestroyStreamSocket(socket);
			throw;
		}
		return transport;
	}
	void close() override
	{
		if (server)
			NET_DestroyServer(server);
		server = nullptr;
	}
	bool listening() const override { return server != nullptr; }
};
} // namespace
std::unique_ptr<NetTransport> makeTcpTransport()
{
	return std::make_unique<TcpTransport>();
}
std::unique_ptr<NetTransportListener> makeTcpTransportListener(const NetListenConfig &config)
{
	return std::make_unique<TcpListener>(config);
}
