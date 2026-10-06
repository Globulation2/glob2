// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetTransport.h"
#include "TlsSetup.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <deque>
#include <openssl/x509v3.h>
#include <boost/beast/http.hpp>
#include <algorithm>
#include <stdexcept>

namespace
{
namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
namespace ws = beast::websocket;
using tcp = asio::ip::tcp;
using Error = boost::system::error_code;
std::string canonicalAddress(const asio::ip::address &address)
{
    if (address.is_v6() && address.to_v6().is_v4_mapped())
        return asio::ip::make_address_v4(asio::ip::v4_mapped, address.to_v6()).to_string();
    return address.to_string();
}

// The same application thread that owns NetConnection pumps asynchronous I/O.
// No blocking connect, TLS handshake, read, or write runs on that thread.
class WssTransport final : public NetTransport
{
	struct Session
	{
		asio::io_context io;
		std::shared_ptr<ssl::context> tls;
		beast::flat_buffer buffer{64 * 1024};
		ws::stream<beast::ssl_stream<beast::tcp_stream>> socket{io, *tls};
		tcp::resolver resolver{io};
		asio::steady_timer connectDeadline{io}, writeDeadline{io};
		State status = State::Connecting;
		std::deque<std::vector<uint8_t>> incoming, outgoing;
		size_t incomingBytes = 0, outgoingBytes = 0;
		std::string host, authority, service, route, fingerprint, failure, peer;
		beast::http::request_parser<beast::http::empty_body> request;
		bool writing = false, readPaused = false;
		// The last poll() ran its whole handler budget: more completions may be queued.
		bool saturated = false;
		// Accepted by a listener (rather than opened by this side).
		bool accepted = false;
		// Unread messages kept before reading pauses (with queueLimit bytes): about
		// three minutes of relay bundles at 25 per second.
		static constexpr size_t incomingMessageLimit = 4096;
		NetMessageMode mode = NetMessageMode::Binary;

		static int verifyPin(X509_STORE_CTX *store, void *data)
		{
			auto &self = *static_cast<Session *>(data);
			X509 *cert = X509_STORE_CTX_get0_cert(store);
			unsigned char digest[EVP_MAX_MD_SIZE];
			unsigned length = 0;
			if (!cert || X509_cmp_current_time(X509_get0_notBefore(cert)) >= 0 ||
				X509_cmp_current_time(X509_get0_notAfter(cert)) <= 0 ||
				!X509_digest(cert, EVP_sha256(), digest, &length) || length != 32)
				return 0;
			std::string actual;
			for (unsigned i = 0; i < length; ++i)
			{
				actual += "0123456789abcdef"[digest[i] >> 4];
				actual += "0123456789abcdef"[digest[i] & 15];
			}
			Error ec;
			asio::ip::make_address(self.host, ec);
			const bool identity =
				!ec ? X509_check_ip_asc(cert, self.host.c_str(), 0) == 1
					: X509_check_host(cert, self.host.c_str(), self.host.size(), 0, nullptr) == 1;
			return identity && CRYPTO_memcmp(actual.data(), self.fingerprint.data(), 64) == 0;
		}
		Session(const std::string &address, const NetTlsConfig &config, NetMessageMode messageMode)
			: tls(NetTls::contextFor(ssl::context::tls_client, config,
							 NetEndpoint::parse(address).fingerprint.empty())),
			  mode(messageMode)
		{
			const auto endpoint = NetEndpoint::parse(address);
			host = endpoint.host;
			authority = endpoint.authority;
			service = endpoint.service;
			route = endpoint.route;
			fingerprint = endpoint.fingerprint;
			socket.next_layer().set_verify_mode(ssl::verify_peer);
			if (!fingerprint.empty())
			{
				SSL_CTX_set_cert_verify_callback(tls->native_handle(), verifyPin, this);
			}
			else
				NetTls::verifyServer(socket.next_layer(), *tls, host, config);
			NetTls::serverName(socket.next_layer(), host);
			setup();
			resolver.async_resolve(
				host, service,
				[this](Error error, tcp::resolver::results_type results)
				{
					if (!active(error))
						return;
					beast::get_lowest_layer(socket).async_connect(
						results,
						[this](Error error, const tcp::endpoint &endpoint)
						{
							if (!active(error))
								return;
							peer = canonicalAddress(endpoint.address());
							socket.next_layer().async_handshake(
								ssl::stream_base::client,
								[this](Error error)
								{
									if (!active(error))
										return;
									socket.set_option(ws::stream_base::timeout{
										std::chrono::seconds(10), std::chrono::seconds(30), true});
									socket.async_handshake(authority, route,
														   [this](Error error)
														   {
															   if (!active(error))
																   return;
															   connected();
														   });
								});
						});
				});
		}
		// Transfer an accepted socket to this session's own I/O context.
		Session(tcp::socket accepted, const NetListenConfig &config,
				std::shared_ptr<ssl::context> serverContext)
			: tls(std::move(serverContext)), mode(config.messageMode)
		{
			this->accepted = true;
			peer = canonicalAddress(accepted.remote_endpoint().address());
			const auto protocol = accepted.local_endpoint().protocol();
			beast::get_lowest_layer(socket).socket().assign(protocol, accepted.release());
			if (config.tls.requireClientCertificate)
				socket.next_layer().set_verify_mode(ssl::verify_peer |
													ssl::verify_fail_if_no_peer_cert);
			setup();
			socket.next_layer().async_handshake(
				ssl::stream_base::server,
				[this, config](Error error)
				{
					if (!active(error))
						return;
					request.header_limit(8192);
					request.body_limit(0);
					beast::http::async_read(
						socket.next_layer(), buffer, request,
						[this, config](Error error, size_t)
						{
							if (!active(error))
								return;
							const auto &req = request.get();
							auto origin = req[beast::http::field::origin];
							if (!ws::is_upgrade(req) || req.target() != config.route ||
								buffer.size() != 0 ||
								(!origin.empty() &&
								 std::find(config.allowedOrigins.begin(),
										   config.allowedOrigins.end(),
										   std::string(origin)) == config.allowedOrigins.end()))
							{
								failure = "WebSocket upgrade, route, or Origin rejected";
								cancel();
								return;
							}
							// Only deployment-configured proxy peers may supply client identity.
							// A single numeric address is required; arbitrary forwarding chains
							// supplied by public clients are never trusted.
							if (std::find(config.trustedProxyAddresses.begin(),
										  config.trustedProxyAddresses.end(),
										  peer) != config.trustedProxyAddresses.end())
							{
								const auto forwarded = req["X-Forwarded-For"];
								if (!forwarded.empty())
								{
									Error invalid;
									auto address =
										asio::ip::make_address(std::string(forwarded), invalid);
									if (invalid)
									{
										failure = "Invalid proxy client address";
										cancel();
										return;
									}
									peer = canonicalAddress(address);
								}
							}
							socket.set_option(ws::stream_base::timeout{
								std::chrono::seconds(10), std::chrono::seconds(30), true});
							socket.async_accept(req,
												[this](Error error)
												{
													if (!active(error))
														return;
													connected();
												});
						});
				});
		}
		void connected()
		{
			connectDeadline.cancel();
			status = State::Connected;
			read();
		}

		void setup()
		{
			const bool binary = mode == NetMessageMode::Binary;
			socket.read_message_max(binary ? 64 * 1024 : textMessageLimit);
			if (!binary)
				buffer.max_size(textMessageLimit);
			socket.binary(binary);
			connectDeadline.expires_after(std::chrono::seconds(10));
			connectDeadline.async_wait(
				[this](Error error)
				{
					if (!error)
					{
						failure = "Connection handshake timed out";
						cancel();
					}
				});
		}
		~Session()
		{
			cancel();
			io.stop();
		}
		bool active(Error error)
		{
			if (status == State::Closed)
				return false; // Preserve the original error across canceled callbacks.
			if (error)
			{
				failure = error.message();
				cancel();
			}
			return status != State::Closed;
		}
		void cancel()
		{
			// Completed writes can still be queued in Winsock. A send shutdown
			// preserves those final bytes before the socket handle is released.
			if (status == State::Connected && outgoingBytes == 0)
			{
				Error ignored;
				beast::get_lowest_layer(socket).socket().shutdown(tcp::socket::shutdown_send, ignored);
			}
			status = State::Closed;
			resolver.cancel();
			connectDeadline.cancel();
			writeDeadline.cancel();
			Error ignored;
			beast::get_lowest_layer(socket).socket().close(ignored);
		}
		void poll()
		{
			io.restart();
			unsigned i = 0;
			while (i < 16 && io.poll_one())
				++i;
			saturated = i == 16;
		}
		NetWaitStatus waitHandles(std::vector<NetWaitHandle> &out)
		{
#ifdef _WIN32
			// Sockets complete through the I/O completion port, which a readiness
			// poll does not see.
			(void)out;
			return NetWaitStatus::Unsupported;
#else
			poll();
			if (status == State::Closed)
				return NetWaitStatus::Idle;
			if (saturated || !incoming.empty())
				return NetWaitStatus::Ready;
			// An outgoing connection resolves on asio's resolver thread and then
			// waits for its own connect and handshake: poll it on a timer.
			if (status == State::Connecting && !accepted)
				return NetWaitStatus::Unsupported;
			auto &lowest = beast::get_lowest_layer(socket).socket();
			if (!lowest.is_open())
				return NetWaitStatus::Idle;
			NetWaitHandle handle;
			handle.socket = static_cast<std::intptr_t>(lowest.native_handle());
			// A handshaking server connection waits for the client's next message.
			handle.read = status == State::Connecting || !readPaused;
			handle.write = writing;
			if (handle.read || handle.write)
				out.push_back(handle);
			return NetWaitStatus::Idle;
#endif
		}
		void read()
		{
			socket.async_read(buffer,
							  [this](Error error, size_t size)
							  {
								  if (!active(error))
									  return;
								  const bool binary = mode == NetMessageMode::Binary;
								  if (socket.got_binary() != binary || size > (binary ? queueLimit : textMessageLimit))
								  {
									  failure = socket.got_binary() != binary
													? (binary ? "Text WebSocket messages are not allowed"
															  : "Binary WebSocket messages are not "
																"allowed in text mode")
													: "Network message too large";
									  cancel();
									  return;
								  }
								  // An empty text message is still a message.
								  if (!size && binary)
								  {
									  read();
									  return;
								  }
								  std::vector<uint8_t> bytes(size);
								  asio::buffer_copy(asio::buffer(bytes), buffer.data());
								  buffer.consume(size);
								  incomingBytes += size;
								  incoming.push_back(std::move(bytes));
								  // A reader that falls behind (a backgrounded phone, a long
								  // reload) is not dropped: reading pauses, TCP flow control
								  // holds the rest at the sender, and takeMessage() resumes
								  // reading once the queue has drained below its bounds.
								  if (incomingFull())
									  readPaused = true;
								  else
									  read();
							  });
		}
		bool incomingFull() const
		{
			return incomingBytes >= (mode == NetMessageMode::Binary ? queueLimit : textQueueLimit) || incoming.size() >= incomingMessageLimit;
		}
		void resumeReading()
		{
			if (readPaused && status == State::Connected && !incomingFull())
			{
				readPaused = false;
				read();
			}
		}
		void write()
		{
			if (writing || outgoing.empty() || status != State::Connected)
				return;
			writing = true;
			writeDeadline.expires_after(std::chrono::seconds(10));
			writeDeadline.async_wait(
				[this](Error error)
				{
					if (!error)
					{
						failure = "WebSocket write timed out";
						cancel();
					}
				});
			socket.async_write(asio::buffer(outgoing.front()),
							   [this](Error error, size_t)
							   {
								   if (!active(error))
									   return;
								   writeDeadline.cancel();
								   outgoingBytes -= outgoing.front().size();
								   outgoing.pop_front();
								   writing = false;
								   write();
							   });
		}
	};
	std::shared_ptr<Session> session;
	NetTlsConfig config;
	std::string failure;
	NetMessageMode mode = NetMessageMode::Binary;

	bool takeMessage(std::vector<uint8_t> &bytes)
	{
		if (!session)
			return false;
		session->poll();
		if (session->incoming.empty())
			return false;
		bytes = std::move(session->incoming.front());
		session->incoming.pop_front();
		session->incomingBytes -= bytes.size();
		session->resumeReading();
		return true;
	}

  public:
	WssTransport(const NetTlsConfig &config, NetMessageMode mode) : config(config), mode(mode) {}
	explicit WssTransport(std::shared_ptr<Session> session)
		: session(std::move(session)), mode(this->session->mode)
	{
	}
	std::string error() const override
	{
		return session ? session->failure : failure;
	}
	std::string peerAddress() const override
	{
		return session ? session->peer : std::string();
	}
	void open(const std::string &address, uint16_t port) override
	{
		close();
		try
		{
			failure.clear();
			session = std::make_shared<Session>(address, config, mode);
		}
		catch (const std::exception &error)
		{
			failure = error.what();
			session.reset();
		}
	}
	void close() override
	{
		if (session)
		{
			if (!session->failure.empty())
				failure = session->failure;
			session->cancel();
		}
		session.reset();
	}
	State state() const override
	{
		if (!session)
			return State::Closed;
		session->poll();
		return session->status;
	}
	size_t pendingOutgoing() const override
	{
		if (!session)
			return 0;
		session->poll();
		return session->status == State::Connected ? session->outgoingBytes : 0;
	}
	bool send(std::vector<uint8_t> bytes) override
	{
		if (mode != NetMessageMode::Binary || state() != State::Connected)
			return false;
		if (bytes.size() > queueLimit - session->outgoingBytes)
		{
			session->failure = "Network output queue overflow";
			return false;
		}
		session->outgoingBytes += bytes.size();
		for (size_t offset = 0; offset < bytes.size(); offset += chunkLimit)
		{
			const auto end = std::min(bytes.size(), offset + chunkLimit);
			session->outgoing.emplace_back(bytes.begin() + offset, bytes.begin() + end);
		}
		session->write();
		return true;
	}
	bool sendText(std::string text) override
	{
		if (mode != NetMessageMode::Text || text.size() > textMessageLimit ||
			text.find('\0') != std::string::npos || state() != State::Connected)
			return false;
		if (text.size() > textQueueLimit - session->outgoingBytes)
		{
			session->failure = "Network output queue overflow";
			return false;
		}
		session->outgoingBytes += text.size();
		session->outgoing.emplace_back(text.begin(), text.end());
		session->write();
		return true;
	}
	bool receiveText(std::string &text) override
	{
		std::vector<uint8_t> message;
		if (mode != NetMessageMode::Text || !takeMessage(message))
			return false;
		text.assign(message.begin(), message.end());
		return true;
	}
	bool receive(std::vector<uint8_t> &bytes) override
	{
		return mode == NetMessageMode::Binary && takeMessage(bytes);
	}
	NetWaitStatus waitHandles(std::vector<NetWaitHandle> &out) const override
	{
		return session ? session->waitHandles(out) : NetWaitStatus::Idle;
	}
	class Listener final : public NetTransportListener
	{
		asio::io_context io;
		tcp::acceptor acceptor{io};
		NetListenConfig config;
		// Immutable after startup; sessions retain it while draining after listener closure.
		std::shared_ptr<ssl::context> tls;
		std::vector<std::shared_ptr<Session>> pending;
		std::vector<std::weak_ptr<Session>> activeSessions;

	  public:
		explicit Listener(const NetListenConfig &c)
			: config(c), tls(NetTls::contextFor(ssl::context::tls_server, c.tls))
		{
			// Fail startup even when no clients have attempted a handshake yet.
			if (c.tls.certificatePem.empty() && c.tls.certificateFile.empty())
				throw std::invalid_argument("A TLS server identity is required");
			if (!c.connectionLimit)
				throw std::invalid_argument("Connection limit must be positive");
			for (auto &proxy : config.trustedProxyAddresses)
                proxy = canonicalAddress(asio::ip::make_address(proxy));
			tcp::endpoint endpoint(asio::ip::make_address(c.bindAddress), c.port);
			acceptor.open(endpoint.protocol());
			acceptor.set_option(asio::socket_base::reuse_address(true));
            if (endpoint.protocol() == tcp::v6()) acceptor.set_option(asio::ip::v6_only(false));
			acceptor.bind(endpoint);
			acceptor.listen();
			acceptor.non_blocking(true);
		}
		std::unique_ptr<NetTransport> accept() override
		{
			activeSessions.erase(std::remove_if(activeSessions.begin(), activeSessions.end(),
												[](const auto &weak)
												{
													auto s = weak.lock();
													return !s || s->status == State::Closed;
												}),
								 activeSessions.end());
			// Bound work performed in a single application tick.
			for (unsigned i = 0;
				 i < 8 && acceptor.is_open() && activeSessions.size() < config.connectionLimit; ++i)
			{
				tcp::socket socket(io);
				Error ec;
				acceptor.accept(socket, ec);
				if (ec == asio::error::would_block || ec == asio::error::try_again)
					break;
				if (ec)
					break;
				try
				{
					auto session = std::make_shared<Session>(std::move(socket), config, tls);
					pending.push_back(session);
					activeSessions.push_back(session);
				}
				catch (const std::exception &)
				{
				}
			}
			for (auto i = pending.begin(); i != pending.end();)
			{
				(*i)->poll();
				if ((*i)->status == State::Closed)
					i = pending.erase(i);
				else if ((*i)->status == State::Connected)
				{
					auto session = *i;
					pending.erase(i);
					return std::make_unique<WssTransport>(std::move(session));
				}
				else
					++i;
			}
			return {};
		}
		void close() override
		{
			Error ec;
			acceptor.close(ec);
			for (auto &session : pending)
				session->cancel();
			pending.clear();
		}
		bool listening() const override
		{
			return acceptor.is_open();
		}
		NetWaitStatus waitHandles(std::vector<NetWaitHandle> &out) const override
		{
#ifdef _WIN32
			(void)out;
			return NetWaitStatus::Unsupported;
#else
			NetWaitStatus result = NetWaitStatus::Idle;
			size_t live = 0;
			for (const auto &weak : activeSessions)
			{
				auto session = weak.lock();
				if (session && session->status != State::Closed)
					++live;
			}
			// At the connection limit, waiting connections stay in the backlog
			// until accept() has room: do not wake for them.
			if (acceptor.is_open() && live < config.connectionLimit)
			{
				NetWaitHandle handle;
				handle.socket = static_cast<std::intptr_t>(
					const_cast<tcp::acceptor &>(acceptor).native_handle());
				handle.read = true;
				out.push_back(handle);
			}
			for (const auto &session : pending)
			{
				if (session->waitHandles(out) != NetWaitStatus::Idle ||
					session->status == State::Connected)
					result = NetWaitStatus::Ready;
			}
			return result;
#endif
		}
	};
};
} // namespace
std::unique_ptr<NetTransport> makeWssTransport(const NetTlsConfig &config, NetMessageMode mode)
{
	return std::make_unique<WssTransport>(config, mode);
}

std::unique_ptr<NetTransportListener> makeWssTransportListener(const NetListenConfig &config)
{
	return std::make_unique<WssTransport::Listener>(config);
}
