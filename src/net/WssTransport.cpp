// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetTransport.h"
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#if defined(GLOB2_MOBILE) || defined(__APPLE__) || defined(_WIN32)
#include "mobile/CertificateTrust.h"
#endif
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
		bool writing = false;

		static void configure(ssl::context &context, const NetTlsConfig &config,
							  bool loadTrust = true)
		{
			if (!SSL_CTX_set_min_proto_version(context.native_handle(), TLS1_2_VERSION))
				throw std::runtime_error("TLS minimum version could not be configured");
			if (!config.certificatePem.empty())
			{
				context.use_certificate_chain(asio::buffer(config.certificatePem));
				context.use_private_key(asio::buffer(config.keyPem), ssl::context::pem);
			}
			else if (!config.certificateFile.empty())
			{
				context.use_certificate_chain_file(config.certificateFile);
				context.use_private_key_file(config.keyFile, ssl::context::pem);
			}
			if ((!config.certificatePem.empty() || !config.certificateFile.empty()) &&
				!SSL_CTX_check_private_key(context.native_handle()))
				throw std::runtime_error("TLS key does not match certificate");
			if (loadTrust)
			{
				if (!config.caPem.empty())
					context.add_certificate_authority(asio::buffer(config.caPem));
				else if (!config.caFile.empty())
					context.load_verify_file(config.caFile);
				else
					context.set_default_verify_paths();
			}
		}
		static std::shared_ptr<ssl::context>
		contextFor(ssl::context::method method, const NetTlsConfig &config, bool loadTrust = true)
		{
			auto context = std::make_shared<ssl::context>(method);
			configure(*context, config, loadTrust);
			return context;
		}
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
		Session(const std::string &address, const NetTlsConfig &config)
			: tls(contextFor(ssl::context::tls_client, config,
							 NetEndpoint::parse(address).fingerprint.empty()))
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
			{
#if defined(GLOB2_MOBILE) || defined(__APPLE__) || defined(_WIN32)
				if (config.caFile.empty() && config.caPem.empty())
					SSL_CTX_set_cert_verify_callback(tls->native_handle(),
													 MobileCertificateTrust::verify, &host);
				else
#endif
					socket.next_layer().set_verify_callback(ssl::host_name_verification(host));
			}
			if (!SSL_set_tlsext_host_name(socket.next_layer().native_handle(), host.c_str()))
				throw std::runtime_error("Could not set TLS server name");
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
							peer = endpoint.address().to_string();
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
			: tls(std::move(serverContext))
		{
			peer = accepted.remote_endpoint().address().to_string();
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
									peer = address.to_string();
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
			socket.read_message_max(64 * 1024);
			socket.binary(true);
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
			for (unsigned i = 0; i < 16 && io.poll_one(); ++i)
			{
			}
		}
		void read()
		{
			socket.async_read(buffer,
							  [this](Error error, size_t size)
							  {
								  if (!active(error))
									  return;
								  if (!socket.got_binary() || size > queueLimit - incomingBytes ||
									  incoming.size() >= 256)
								  {
									  failure = !socket.got_binary()
													? "Text WebSocket messages are not allowed"
													: "Network input queue overflow";
									  cancel();
									  return;
								  }
								  if (!size)
								  {
									  read();
									  return;
								  }
								  std::vector<uint8_t> bytes(size);
								  asio::buffer_copy(asio::buffer(bytes), buffer.data());
								  buffer.consume(size);
								  incomingBytes += size;
								  incoming.push_back(std::move(bytes));
								  read();
							  });
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

  public:
	explicit WssTransport(const NetTlsConfig &config) : config(config) {}
	explicit WssTransport(std::shared_ptr<Session> session) : session(std::move(session)) {}
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
			session = std::make_shared<Session>(address, config);
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
	bool send(std::vector<uint8_t> bytes) override
	{
		if (state() != State::Connected)
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
	bool receive(std::vector<uint8_t> &bytes) override
	{
		if (!session)
			return false;
		session->poll();
		if (session->incoming.empty())
			return false;
		bytes = std::move(session->incoming.front());
		session->incoming.pop_front();
		session->incomingBytes -= bytes.size();
		return true;
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
			: config(c), tls(Session::contextFor(ssl::context::tls_server, c.tls))
		{
			// Fail startup even when no clients have attempted a handshake yet.
			if (c.tls.certificatePem.empty() && c.tls.certificateFile.empty())
				throw std::invalid_argument("A TLS server identity is required");
			if (!c.connectionLimit)
				throw std::invalid_argument("Connection limit must be positive");
			for (const auto &proxy : c.trustedProxyAddresses)
				asio::ip::make_address(proxy);
			tcp::endpoint endpoint(asio::ip::make_address(c.bindAddress), c.port);
			acceptor.open(endpoint.protocol());
			acceptor.set_option(asio::socket_base::reuse_address(true));
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
	};
};
} // namespace
std::unique_ptr<NetTransport> makeWssTransport(const NetTlsConfig &config)
{
	return std::make_unique<WssTransport>(config);
}

std::unique_ptr<NetTransportListener> makeNetTransportListener(const NetListenConfig &config)
{
	return std::make_unique<WssTransport::Listener>(config);
}
