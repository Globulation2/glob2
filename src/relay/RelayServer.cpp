// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "RelayServer.h"

#include "JwksStore.h"
#include "MatchDirectory.h"
#include "MatchReport.h"
#include "RelayLog.h"
#include "RelayMetrics.h"
#include "TicketVerifier.h"
#include "TurnSequencer.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <openssl/ssl.h>

#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <set>

namespace Relay
{
namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using Turn::PeerId;

namespace
{
	std::uint64_t monotonicMicros()
	{
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	/// Largest byte-stream backlog a connection may hold: one maximal frame being
	/// assembled plus the rest of the WebSocket message that carried its start.
	constexpr std::size_t MAX_PENDING_BYTES = 2 * (Turn::MAX_FRAME_BYTES + 2);
	/// Read limit for one WebSocket message; WssTransport sends 16 KiB chunks.
	constexpr std::size_t MAX_WS_MESSAGE_BYTES = 64 * 1024;
	// Round-trip probe: a WebSocket ping (a transport control frame, outside the turn
	// protocol) every GLOB2_RELAY_RTT_PING_MS on a connection that has joined a match.
	// Every client transport answers pings itself (Beast, browsers). Telemetry only.
	/// Payload marker that tells our probes from Beast's own keep-alive pings.
	constexpr char RTT_PING_MARKER = 'g';
	constexpr std::size_t RTT_PING_BYTES = 9;

	std::vector<std::uint8_t> rejectPayload(Turn::RejectReason reason, const std::string& detail)
	{
		Turn::Reject r;
		r.reason = reason;
		r.detail = detail.substr(0, Turn::MAX_REJECT_DETAIL_BYTES);
		return Turn::TurnCodec::encode(r);
	}
}

class Match;

/// A match WebSocket. Frames are NetConnection frames (u16 length + payload) carried
/// as a byte stream across binary WebSocket messages, exactly as NetConnection sends
/// them over WssTransport.
class Connection : public std::enable_shared_from_this<Connection>
{
public:
	Connection(RelayServer::Impl& server, PeerId id, std::string address)
		: server(server), id(id), address(std::move(address))
	{
	}
	virtual ~Connection() = default;

	/// Queues one frame payload. Drops the connection if its backlog overflows.
	virtual void sendFrame(const std::vector<std::uint8_t>& payload) = 0;
	/// Sends what is queued, then closes. No further frames reach the match.
	virtual void closeAfterFlush() = 0;
	/// Closes the socket at once.
	virtual void abort() = 0;
	void reject(Turn::RejectReason reason, const std::string& detail)
	{
		sendFrame(rejectPayload(reason, detail));
		closeAfterFlush();
	}

	RelayServer::Impl& server;
	const PeerId id;
	const std::string address;
	std::shared_ptr<Match> match;
	bool detached = false; ///< closing; frames are no longer delivered to the match
	bool closed = false;
};

/// One match: a TurnSequencer, the connections of its seats and the timer that drives
/// it. Every call happens on the event-loop thread.
class Match final : public Turn::SequencerOutput, public std::enable_shared_from_this<Match>
{
public:
	Match(RelayServer::Impl& server, const TicketClaims& first, const Turn::SequencerConfig& config)
		: server(server), id(first.matchId), simVersion(first.simVersion), startedAt(unixNow()),
		  sequencer(config, first.humanSeatMask, [this](const std::string&) { return pendingSeat; }, *this,
		            monotonicMicros())
	{
	}

	void start(asio::any_io_executor executor);
	void attach(const std::shared_ptr<Connection>& connection, int seat, const std::vector<std::uint8_t>& hello);
	void receive(Connection& connection, const std::vector<std::uint8_t>& payload);
	void connectionClosed(Connection& connection);
	void abortNow();
	/// A WebSocket round trip measured on the connection (telemetry only).
	void roundTrip(const Connection& connection, std::uint64_t micros)
	{
		if (!ended && peers.count(connection.id))
			sequencer.transportRoundTrip(connection.id, micros);
	}

	void send(PeerId peer, const std::vector<std::uint8_t>& payload) override
	{
		auto it = peers.find(peer);
		if (it != peers.end())
			it->second->sendFrame(payload);
	}
	void close(PeerId peer) override
	{
		auto it = peers.find(peer);
		if (it == peers.end())
			return;
		auto connection = it->second;
		peers.erase(it);
		connection->closeAfterFlush();
	}

	RelayServer::Impl& server;
	const std::string id;
	const SimVersion simVersion;
	const std::int64_t startedAt;
	Turn::TurnSequencer sequencer;
	std::optional<std::string> setupJson;
	bool gameFinished = false;
	bool aborted = false;
	bool ended = false;
	std::size_t connectionCount() const { return peers.size(); }

private:
	asio::awaitable<void> tickLoop();
	void afterEvent();

	std::map<PeerId, std::shared_ptr<Connection>> peers;
	int pendingSeat = -1;
	std::unique_ptr<asio::steady_timer> timer;
};

struct RelayServer::Impl : std::enable_shared_from_this<RelayServer::Impl>
{
	Impl(asio::io_context& io, const RelayConfig& config, RelayMetrics& metrics, JwksStore& jwks,
	     PlatformLink& platform, RelayServer& owner)
		: io(io), config(config), metrics(metrics), jwks(jwks), platform(platform), owner(owner), acceptor(io),
		  drainTimer(io)
	{
		sequencerConfig.graceMicros = static_cast<std::uint64_t>(config.graceSeconds) * 1000000ull;
		sequencerConfig.startBarrierMicros = static_cast<std::uint64_t>(config.loadWaitSeconds) * 1000000ull;
		if (config.tlsEnabled())
		{
			tls = std::make_unique<ssl::context>(ssl::context::tls_server);
			SSL_CTX_set_min_proto_version(tls->native_handle(), TLS1_2_VERSION);
			tls->use_certificate_chain_file(config.tlsCertificateFile);
			tls->use_private_key_file(config.tlsKeyFile, ssl::context::pem);
			if (!SSL_CTX_check_private_key(tls->native_handle()))
				throw std::runtime_error("GLOB2_RELAY_TLS_KEY does not match GLOB2_RELAY_TLS_CERT");
		}
	}

	asio::io_context& io;
	const RelayConfig& config;
	RelayMetrics& metrics;
	JwksStore& jwks;
	PlatformLink& platform;
	RelayServer& owner;
	tcp::acceptor acceptor;
	std::unique_ptr<ssl::context> tls;
	Turn::SequencerConfig sequencerConfig;
	MatchDirectory directory;
	std::map<std::string, std::shared_ptr<Match>> matches;
	std::map<std::string, std::size_t> perAddress;
	std::size_t sockets = 0;      ///< accepted sockets, including those still handshaking
	std::size_t connections = 0;  ///< upgraded match connections
	std::size_t finalizing = 0;   ///< ended matches whose record is being assembled
	PeerId nextPeer = 1;
	bool draining = false;
	bool drainedNotified = false;
	asio::steady_timer drainTimer;

	std::uint16_t start();
	asio::awaitable<void> acceptLoop();
	asio::awaitable<void> serveSocket(tcp::socket socket);
	template <class Next>
	asio::awaitable<void> serveHttp(Next stream, std::string peer);
	template <class Body>
	http::response<http::string_body> plainResponse(const http::request<Body>& request);
	asio::awaitable<void> admit(std::shared_ptr<Connection> connection, std::string ticket,
	                            std::vector<std::uint8_t> hello);
	void connectionOpened(const Connection& c)
	{
		++connections;
		++perAddress[c.address];
		++metrics.connectionsAccepted;
		metrics.connections = static_cast<std::int64_t>(connections);
	}
	void connectionClosed(Connection& c)
	{
		--connections;
		auto it = perAddress.find(c.address);
		if (it != perAddress.end() && --it->second == 0)
			perAddress.erase(it);
		metrics.connections = static_cast<std::int64_t>(connections);
		if (c.match)
		{
			auto match = std::move(c.match);
			match->connectionClosed(c);
		}
	}
	void matchEnded(const std::shared_ptr<Match>& match);
	asio::awaitable<void> finalize(std::shared_ptr<Match> match);
	void checkDrained();
	LoadSnapshot snapshot() const
	{
		LoadSnapshot s;
		s.load.matches = matches.size();
		s.load.connections = connections;
		s.draining = draining;
		for (const auto& m : matches)
			s.activeMatchIds.push_back(m.first);
		return s;
	}
};

// ---------------------------------------------------------------------------
// Match

void Match::start(asio::any_io_executor executor)
{
	timer = std::make_unique<asio::steady_timer>(executor);
	asio::co_spawn(executor, [self = shared_from_this()]() { return self->tickLoop(); }, asio::detached);
}

asio::awaitable<void> Match::tickLoop()
{
	// Wake when the next live bundle is due (TurnSequencer::nextBundleMicros), so
	// bundles leave on their tick boundary instead of up to a timer period late, which
	// clients would see as jitter. Grace expiry, arbitration timeouts and presence
	// need no more than the 10 ms fallback, also used while no bundle is due (before
	// the first tick, or once the sequencer stops sending).
	constexpr std::uint64_t FALLBACK_MICROS = 10000;
	while (!ended)
	{
		const std::uint64_t now = monotonicMicros();
		std::uint64_t wake = now + FALLBACK_MICROS;
		const std::uint64_t next = sequencer.nextBundleMicros();
		if (next > now && next < wake)
			wake = next;
		timer->expires_at(std::chrono::steady_clock::time_point(std::chrono::microseconds(wake)));
		boost::system::error_code ignored;
		co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, ignored));
		if (ended)
			break;
		sequencer.update(monotonicMicros());
		afterEvent();
	}
}

void Match::attach(const std::shared_ptr<Connection>& connection, int seat, const std::vector<std::uint8_t>& hello)
{
	const auto now = monotonicMicros();
	connection->match = shared_from_this();
	peers[connection->id] = connection;
	sequencer.onConnect(connection->id, now);
	pendingSeat = seat;
	sequencer.onReceive(connection->id, hello, now);
	pendingSeat = -1;
	afterEvent();
}

void Match::receive(Connection& connection, const std::vector<std::uint8_t>& payload)
{
	if (!peers.count(connection.id))
		return;
	// The sequencer treats both quit reasons alike; the relay remembers a finished game
	// so the platform can tell a completed match from an abandoned one.
	if (!payload.empty() && payload[0] == Turn::MSG_QUIT)
	{
		auto message = Turn::TurnCodec::decode(payload);
		if (message && static_cast<const Turn::Quit&>(*message).reason == Turn::QuitReason::GameFinished)
			gameFinished = true;
	}
	sequencer.onReceive(connection.id, payload, monotonicMicros());
	afterEvent();
}

void Match::connectionClosed(Connection& connection)
{
	if (peers.erase(connection.id))
		sequencer.onDisconnect(connection.id, monotonicMicros());
	afterEvent();
}

void Match::abortNow()
{
	if (ended)
		return;
	aborted = true;
	sequencer.finish(monotonicMicros());
	afterEvent();
}

void Match::afterEvent()
{
	if (ended || !sequencer.matchOver())
		return;
	ended = true;
	if (timer)
		timer->cancel();
	auto remaining = std::move(peers);
	peers.clear();
	for (auto& p : remaining)
		p.second->closeAfterFlush();
	server.matchEnded(shared_from_this());
}

// ---------------------------------------------------------------------------
// WebSocket connection

template <class Next>
class WsConnection final : public Connection
{
public:
	WsConnection(RelayServer::Impl& server, PeerId id, std::string address, Next stream)
		: Connection(server, id, std::move(address)), ws(std::move(stream)),
		  helloDeadline(ws.get_executor()), closeDeadline(ws.get_executor()), pingTimer(ws.get_executor())
	{
	}

	asio::awaitable<void> run(http::request<http::empty_body> request)
	{
		auto self = shared_from_this();
		ws.read_message_max(MAX_WS_MESSAGE_BYTES);
		ws.binary(true);
		ws.set_option(websocket::stream_base::timeout{std::chrono::seconds(10), std::chrono::seconds(30), true});
		ws.set_option(websocket::stream_base::decorator(
			[](websocket::response_type& res) { res.set(http::field::server, "glob2-relay"); }));
		ws.control_callback([this](websocket::frame_type kind, beast::string_view payload) {
			if (kind == websocket::frame_type::pong)
				pongReceived(payload);
		});
		try
		{
			co_await ws.async_accept(request, asio::use_awaitable);
		}
		catch (const std::exception&)
		{
			closed = true;
			co_return;
		}
		server.connectionOpened(*this);
		helloDeadline.expires_after(std::chrono::seconds(server.config.helloTimeoutSeconds));
		helloDeadline.async_wait([weak = weak_from_this()](boost::system::error_code ec) {
			auto c = weak.lock();
			if (!ec && c && !c->match && !c->closed)
				c->abort();
		});
		tokens = static_cast<double>(server.config.frameBurst);
		lastRefill = std::chrono::steady_clock::now();
		if (server.config.rttPingMillis > 0)
			asio::co_spawn(ws.get_executor(), pingLoop(), asio::detached);
		try
		{
			co_await readLoop();
		}
		catch (const std::exception&)
		{
		}
		closed = true;
		detached = true;
		helloDeadline.cancel();
		closeDeadline.cancel();
		pingTimer.cancel();
		server.connectionClosed(*this);
	}

	void sendFrame(const std::vector<std::uint8_t>& payload) override
	{
		if (closed || closing)
			return;
		if (payload.empty() || payload.size() > Turn::MAX_FRAME_BYTES)
			return;
		if (outBytes + payload.size() + 2 > server.config.maxOutgoingBytes)
		{
			++server.metrics.slowReadersDropped;
			logLine("info", "Dropping connection " + std::to_string(id) + " from " + address + ": send queue full");
			abort();
			return;
		}
		auto frame = std::make_shared<std::vector<std::uint8_t>>(payload.size() + 2);
		(*frame)[0] = static_cast<std::uint8_t>(payload.size() >> 8);
		(*frame)[1] = static_cast<std::uint8_t>(payload.size() & 0xff);
		std::copy(payload.begin(), payload.end(), frame->begin() + 2);
		outBytes += frame->size();
		outgoing.push_back(std::move(frame));
		pump();
	}

	void closeAfterFlush() override
	{
		detached = true;
		if (closing || closed)
			return;
		closing = true;
		pump();
		closeDeadline.expires_after(std::chrono::seconds(5));
		closeDeadline.async_wait([weak = weak_from_this()](boost::system::error_code ec) {
			if (auto c = weak.lock(); c && !ec)
				c->abort();
		});
	}

	void abort() override
	{
		detached = true;
		boost::system::error_code ignored;
		beast::get_lowest_layer(ws).socket().close(ignored);
	}

private:
	void pump()
	{
		if (writing || closed)
			return;
		if (outgoing.empty())
		{
			if (closing && !closeSent)
			{
				closeSent = true;
				writing = true;
				ws.async_close(websocket::close_code::normal,
				               [self = shared_from_this()](boost::system::error_code) {
					               auto& c = static_cast<WsConnection&>(*self);
					               c.writing = false;
					               c.abort();
				               });
			}
			return;
		}
		writing = true;
		auto frame = outgoing.front();
		ws.async_write(asio::buffer(*frame), [self = shared_from_this(), frame](boost::system::error_code ec, std::size_t) {
			auto& c = static_cast<WsConnection&>(*self);
			c.writing = false;
			if (ec)
			{
				c.abort();
				return;
			}
			c.outBytes -= frame->size();
			c.outgoing.pop_front();
			++c.server.metrics.framesOut;
			c.server.metrics.bytesOut += frame->size() - 2;
			c.pump();
		});
	}

	/// Pings the client every rttPingMillis once it has joined a match; the pong
	/// echoes the send time, so a lost or late pong never mismatches.
	asio::awaitable<void> pingLoop()
	{
		auto self = shared_from_this();
		for (;;)
		{
			pingTimer.expires_after(std::chrono::milliseconds(server.config.rttPingMillis));
			boost::system::error_code ec;
			co_await pingTimer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
			if (ec || closed || closing || detached)
				co_return;
			if (!match || pinging)
				continue;
			char payload[RTT_PING_BYTES];
			payload[0] = RTT_PING_MARKER;
			const std::uint64_t sent = monotonicMicros();
			for (std::size_t i = 0; i < 8; ++i)
				payload[1 + i] = static_cast<char>((sent >> (56 - 8 * i)) & 0xff);
			pinging = true;
			ws.async_ping(websocket::ping_data(payload, RTT_PING_BYTES), [self](boost::system::error_code) {
				static_cast<WsConnection&>(*self).pinging = false;
			});
		}
	}

	void pongReceived(beast::string_view payload)
	{
		if (payload.size() != RTT_PING_BYTES || payload[0] != RTT_PING_MARKER || detached || !match)
			return;
		std::uint64_t sent = 0;
		for (std::size_t i = 0; i < 8; ++i)
			sent = (sent << 8) | static_cast<std::uint8_t>(payload[1 + i]);
		const std::uint64_t now = monotonicMicros();
		if (sent > now || now - sent > 60000000ull)
			return;
		auto m = match;
		m->roundTrip(*this, now - sent);
	}

	bool takeToken()
	{
		const auto now = std::chrono::steady_clock::now();
		const double elapsed = std::chrono::duration<double>(now - lastRefill).count();
		lastRefill = now;
		tokens = std::min(static_cast<double>(server.config.frameBurst),
		                  tokens + elapsed * static_cast<double>(server.config.framesPerSecond));
		if (tokens < 1.0)
			return false;
		tokens -= 1.0;
		return true;
	}

	asio::awaitable<void> readLoop()
	{
		beast::flat_buffer buffer;
		std::vector<std::uint8_t> pending;
		for (;;)
		{
			buffer.clear();
			co_await ws.async_read(buffer, asio::use_awaitable);
			if (detached)
				continue; // drain the socket until the close completes
			if (!ws.got_binary())
			{
				reject(Turn::RejectReason::Malformed, "Text WebSocket messages are not allowed");
				continue;
			}
			const auto data = buffer.data();
			const auto* bytes = static_cast<const std::uint8_t*>(data.data());
			if (pending.size() + data.size() > MAX_PENDING_BYTES)
			{
				reject(Turn::RejectReason::Malformed, "Frame stream overflow");
				continue;
			}
			pending.insert(pending.end(), bytes, bytes + data.size());
			std::size_t offset = 0;
			while (!detached && pending.size() - offset >= 2)
			{
				const std::size_t length = (std::size_t(pending[offset]) << 8) | pending[offset + 1];
				if (length == 0)
				{
					reject(Turn::RejectReason::Malformed, "Empty frame");
					break;
				}
				if (pending.size() - offset - 2 < length)
					break;
				std::vector<std::uint8_t> payload(pending.begin() + offset + 2, pending.begin() + offset + 2 + length);
				offset += length + 2;
				++server.metrics.framesIn;
				server.metrics.bytesIn += length;
				if (!takeToken())
				{
					++server.metrics.floodingDropped;
					reject(Turn::RejectReason::Flooding, "Too many messages");
					break;
				}
				co_await handleFrame(std::move(payload));
			}
			pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(std::min(offset, pending.size())));
		}
	}

	asio::awaitable<void> handleFrame(std::vector<std::uint8_t> payload)
	{
		if (detached)
			co_return;
		if (match)
		{
			auto m = match;
			m->receive(*this, payload);
			co_return;
		}
		auto message = Turn::TurnCodec::decode(payload);
		if (!message || message->getMessageType() != Turn::MSG_HELLO)
		{
			reject(Turn::RejectReason::Malformed, "Expected Hello");
			co_return;
		}
		const auto& hello = static_cast<const Turn::Hello&>(*message);
		if (!Turn::supportedProtocol(hello.protocolVersion))
		{
			reject(Turn::RejectReason::ProtocolVersion, "Turn protocol version mismatch");
			co_return;
		}
		// Reading pauses while the ticket is verified, so frames that follow the Hello
		// wait in the socket and reach the match in order.
		co_await server.admit(shared_from_this(), hello.ticket, payload);
	}

	websocket::stream<Next> ws;
	asio::steady_timer helloDeadline;
	asio::steady_timer closeDeadline;
	asio::steady_timer pingTimer;
	bool pinging = false;
	std::deque<std::shared_ptr<std::vector<std::uint8_t>>> outgoing;
	std::size_t outBytes = 0;
	bool writing = false;
	bool closing = false;
	bool closeSent = false;
	double tokens = 0;
	std::chrono::steady_clock::time_point lastRefill;
};

// ---------------------------------------------------------------------------
// Admission and match lifecycle

asio::awaitable<void> RelayServer::Impl::admit(std::shared_ptr<Connection> connection, std::string ticket,
                                               std::vector<std::uint8_t> hello)
{
	VerifyOptions options;
	options.leewaySeconds = config.leewaySeconds;
	options.issuer = config.issuer;
	options.nowSeconds = unixNow();
	auto keys = jwks.current();
	TicketResult result;
	if (keys)
		result = verifyTicket(ticket, *keys, options);
	else
		result.error = TicketError::Key;
	if (result.error == TicketError::Key)
	{
		// Key rotation: a ticket signed with a key we have not seen yet triggers a
		// (rate-limited) JWKS refresh before it is refused.
		const std::string kid = result.kid.empty() ? ticketKeyId(ticket) : result.kid;
		co_await jwks.refreshForUnknownKid(kid);
		if ((keys = jwks.current()))
		{
			options.nowSeconds = unixNow();
			result = verifyTicket(ticket, *keys, options);
		}
	}
	if (connection->detached)
		co_return;
	if (!result.ok())
	{
		metrics.ticketRejected(ticketErrorName(result.error));
		logLine("info", "Refused a ticket from " + connection->address + ": " + ticketErrorName(result.error) + " (" +
		                    result.detail + ")");
		connection->reject(Turn::RejectReason::BadTicket, std::string("Ticket refused: ") + ticketErrorName(result.error));
		co_return;
	}
	const TicketClaims& claims = result.claims;
	// The platform names the relay a match is allocated to; a ticket is good only there.
	if (!config.publicUrl.empty() && claims.relayUrl != config.publicUrl)
	{
		metrics.ticketRejected("wrong_relay");
		logLine("info", "Refused a ticket for relay " + claims.relayUrl.substr(0, 200) + " from " + connection->address);
		connection->reject(Turn::RejectReason::BadTicket, "Ticket is for another relay");
		co_return;
	}
	directory.prune(unixNow());
	const Admission admission = directory.check(claims, draining, config.maxMatches);
	switch (admission)
	{
	case Admission::Ended:
		connection->reject(Turn::RejectReason::MatchOver, "The match is over");
		co_return;
	case Admission::Draining:
		connection->reject(Turn::RejectReason::MatchOver, "This relay is draining; ask the platform for another relay");
		co_return;
	case Admission::Full:
		connection->reject(Turn::RejectReason::MatchOver, "This relay is full; ask the platform for another relay");
		co_return;
	case Admission::SimVersionDiffers:
	case Admission::HumanSeatsDiffer:
		metrics.ticketRejected(admissionName(admission));
		logLine("warning", "Ticket for match " + claims.matchId + " disagrees with the match: " + admissionName(admission));
		connection->reject(Turn::RejectReason::BadTicket,
		                   admission == Admission::SimVersionDiffers ? "Ticket sim version differs from the match"
		                                                             : "Ticket human seats differ from the match");
		co_return;
	case Admission::Create:
	{
		directory.create(claims);
		auto match = std::make_shared<Match>(*this, claims, sequencerConfig);
		matches[claims.matchId] = match;
		match->start(io.get_executor());
		++metrics.matchesStarted;
		metrics.matches = static_cast<std::int64_t>(matches.size());
		logLine("info", "Match " + claims.matchId + " started (sim " + claims.simVersion.key() + ")");
		if (platform.enabled())
			asio::co_spawn(io,
			               [this, match]() -> asio::awaitable<void> {
				               auto setup = co_await platform.fetchSetup(match->id);
				               if (setup && !match->setupJson)
					               match->setupJson = std::move(setup);
			               },
			               asio::detached);
		break;
	}
	case Admission::Join:
		directory.sawTicket(claims);
		break;
	}
	auto it = matches.find(claims.matchId);
	if (it == matches.end())
	{
		connection->reject(Turn::RejectReason::MatchOver, "The match is over");
		co_return;
	}
	it->second->attach(connection, claims.seat, hello);
}

void RelayServer::Impl::matchEnded(const std::shared_ptr<Match>& match)
{
	directory.end(match->id, unixNow(), config.leewaySeconds);
	matches.erase(match->id);
	metrics.matches = static_cast<std::int64_t>(matches.size());
	++finalizing;
	asio::co_spawn(io, finalize(match), asio::detached);
}

asio::awaitable<void> RelayServer::Impl::finalize(std::shared_ptr<Match> match)
{
	if (!match->setupJson && platform.enabled())
		match->setupJson = co_await platform.fetchSetup(match->id);
	std::array<std::uint8_t, 32> mapHash{};
	std::string setup = match->setupJson.value_or("");
	if (!setup.empty() && !setupMapHash(setup, mapHash))
		logLine("warning", "Setup of match " + match->id + " has no valid map.hash");
	if (setup.size() > Turn::MatchRecord::MAX_SETUP_BYTES)
		setup.clear();
	Turn::MatchRecord record = match->sequencer.buildRecord(match->id, match->simVersion.key(), setup, mapHash);
	std::vector<std::uint8_t> bytes;
	try
	{
		bytes = record.serialize();
	}
	catch (const std::exception& e)
	{
		logLine("error", "Match record of " + match->id + " could not be serialized: " + e.what());
		record.setupJson.clear();
		bytes = record.serialize();
	}
	MatchEndInfo info;
	info.matchId = match->id;
	info.relayId = config.relayId;
	info.simVersion = match->simVersion;
	info.startedAt = match->startedAt;
	info.endedAt = unixNow();
	info.reason = match->aborted ? EndReason::Aborted : match->gameFinished ? EndReason::Completed : EndReason::Abandoned;
	info.network = match->sequencer.networkSummary();
	const auto& stats = match->sequencer.stats();
	metrics.ordersSequenced += stats.ordersSequenced;
	metrics.bundlesSent += stats.bundlesSent;
	metrics.matchNetwork(match->sequencer.telemetry());
	metrics.matchEnded(endReasonName(info.reason));
	logLine("info", "Match " + match->id + " ended (" + endReasonName(info.reason) + ") at tick " +
	                    std::to_string(record.endTick) + "; record " + std::to_string(bytes.size()) + " bytes");
	FinishedMatch finished;
	finished.matchId = match->id;
	finished.endedJson = matchEndedJson(info, record, bytes);
	finished.record = std::move(bytes);
	platform.submit(std::move(finished));
	--finalizing;
	checkDrained();
}

void RelayServer::Impl::checkDrained()
{
	if (draining && matches.empty() && finalizing == 0 && !drainedNotified)
	{
		drainedNotified = true;
		drainTimer.cancel();
		if (owner.onDrained)
			asio::post(io, owner.onDrained);
	}
}

// ---------------------------------------------------------------------------
// Listener and HTTP

std::uint16_t RelayServer::Impl::start()
{
	tcp::endpoint endpoint(asio::ip::make_address(config.bindAddress), config.port);
	acceptor.open(endpoint.protocol());
	acceptor.set_option(asio::socket_base::reuse_address(true));
	acceptor.bind(endpoint);
	acceptor.listen(asio::socket_base::max_listen_connections);
	asio::co_spawn(io, [self = shared_from_this()]() { return self->acceptLoop(); }, asio::detached);
	return acceptor.local_endpoint().port();
}

asio::awaitable<void> RelayServer::Impl::acceptLoop()
{
	while (acceptor.is_open())
	{
		boost::system::error_code ec;
		tcp::socket socket = co_await acceptor.async_accept(asio::redirect_error(asio::use_awaitable, ec));
		if (ec)
		{
			if (!acceptor.is_open())
				break;
			continue;
		}
		// Bound the sockets still handshaking as well as the upgraded ones.
		if (sockets >= config.maxConnections + 64)
		{
			++metrics.connectionsRefused;
			boost::system::error_code ignored;
			socket.close(ignored);
			continue;
		}
		asio::co_spawn(io, serveSocket(std::move(socket)), asio::detached);
	}
}

asio::awaitable<void> RelayServer::Impl::serveSocket(tcp::socket socket)
{
	++sockets;
	struct Count
	{
		std::size_t& n;
		~Count() { --n; }
	} count{sockets};
	boost::system::error_code ec;
	const auto remote = socket.remote_endpoint(ec);
	if (ec)
		co_return;
	std::string peer = remote.address().to_string();
	try
	{
		if (tls)
		{
			beast::ssl_stream<beast::tcp_stream> stream(beast::tcp_stream(std::move(socket)), *tls);
			beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(10));
			co_await stream.async_handshake(ssl::stream_base::server, asio::use_awaitable);
			co_await serveHttp(std::move(stream), std::move(peer));
		}
		else
			co_await serveHttp(beast::tcp_stream(std::move(socket)), std::move(peer));
	}
	catch (const std::exception&)
	{
	}
}

template <class Body>
http::response<http::string_body> RelayServer::Impl::plainResponse(const http::request<Body>& request)
{
	http::response<http::string_body> response;
	response.version(request.version());
	response.keep_alive(false);
	response.set(http::field::server, "glob2-relay");
	response.set(http::field::content_type, "text/plain; charset=utf-8");
	response.set(http::field::cache_control, "no-store");
	std::string target(request.target());
	target = target.substr(0, target.find('?'));
	if (request.method() != http::verb::get && request.method() != http::verb::head)
	{
		response.result(http::status::method_not_allowed);
		response.body() = "method not allowed\n";
	}
	else if (target == "/healthz")
	{
		response.result(http::status::ok);
		response.body() = draining ? "draining\n" : "ok\n";
	}
	else if (target == "/readyz")
	{
		const bool ready = !draining && jwks.ready();
		response.result(ready ? http::status::ok : http::status::service_unavailable);
		response.body() = draining ? "draining\n" : ready ? "ready\n" : "no ticket keys\n";
	}
	else if (target == "/metrics")
	{
		const std::string auth(request[http::field::authorization]);
		if (!config.metricsToken.empty() && auth != "Bearer " + config.metricsToken)
		{
			response.result(http::status::unauthorized);
			response.body() = "unauthorized\n";
		}
		else
		{
			response.result(http::status::ok);
			response.set(http::field::content_type, "text/plain; version=0.0.4; charset=utf-8");
			response.body() = metrics.render();
		}
	}
	else
	{
		response.result(http::status::not_found);
		response.body() = "not found\n";
	}
	response.prepare_payload();
	return response;
}

template <class Next>
asio::awaitable<void> RelayServer::Impl::serveHttp(Next stream, std::string peer)
{
	beast::flat_buffer buffer;
	http::request_parser<http::empty_body> parser;
	parser.header_limit(8192);
	parser.body_limit(0);
	beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(10));
	co_await http::async_read(stream, buffer, parser, asio::use_awaitable);
	http::request<http::empty_body> request = parser.release();

	auto refuse = [&](http::status status, const char* why) -> asio::awaitable<void> {
		++metrics.connectionsRefused;
		http::response<http::string_body> response{status, request.version()};
		response.keep_alive(false);
		response.set(http::field::server, "glob2-relay");
		response.set(http::field::content_type, "text/plain; charset=utf-8");
		response.body() = std::string(why) + "\n";
		response.prepare_payload();
		co_await http::async_write(stream, response, asio::use_awaitable);
	};

	if (!websocket::is_upgrade(request))
	{
		auto response = plainResponse(request);
		co_await http::async_write(stream, response, asio::use_awaitable);
		co_return;
	}
	std::string target(request.target());
	if (target.substr(0, target.find('?')) != config.route)
	{
		co_await refuse(http::status::not_found, "unknown route");
		co_return;
	}
	// Browsers always send Origin; native clients never do.
	const std::string origin(request[http::field::origin]);
	if (!origin.empty() && std::find(config.allowedOrigins.begin(), config.allowedOrigins.end(), origin) ==
	                           config.allowedOrigins.end() &&
	    std::find(config.allowedOrigins.begin(), config.allowedOrigins.end(), "*") == config.allowedOrigins.end())
	{
		co_await refuse(http::status::forbidden, "origin not allowed");
		co_return;
	}
	// Only a deployment-configured proxy may name the client; its last hop is the one
	// it appended itself.
	if (std::find(config.trustedProxies.begin(), config.trustedProxies.end(), peer) != config.trustedProxies.end())
	{
		std::string forwarded(request["X-Forwarded-For"]);
		if (!forwarded.empty())
		{
			std::string last = forwarded.substr(forwarded.rfind(',') == std::string::npos ? 0 : forwarded.rfind(',') + 1);
			last.erase(0, last.find_first_not_of(' '));
			last.erase(last.find_last_not_of(' ') + 1);
			boost::system::error_code invalid;
			const auto address = asio::ip::make_address(last, invalid);
			if (invalid)
			{
				co_await refuse(http::status::bad_request, "invalid X-Forwarded-For");
				co_return;
			}
			peer = address.to_string();
		}
	}
	if (connections >= config.maxConnections)
	{
		co_await refuse(http::status::service_unavailable, "relay connection limit reached");
		co_return;
	}
	auto known = perAddress.find(peer);
	if (known != perAddress.end() && known->second >= config.maxConnectionsPerAddress)
	{
		co_await refuse(http::status::too_many_requests, "too many connections from this address");
		co_return;
	}
	if (buffer.size() != 0)
	{
		co_await refuse(http::status::bad_request, "data before the WebSocket handshake");
		co_return;
	}
	beast::get_lowest_layer(stream).expires_never();
	auto connection = std::make_shared<WsConnection<Next>>(*this, nextPeer++, peer, std::move(stream));
	co_await connection->run(std::move(request));
}

// ---------------------------------------------------------------------------
// RelayServer

RelayServer::RelayServer(asio::io_context& io, const RelayConfig& config, RelayMetrics& metrics, JwksStore& jwks,
                         PlatformLink& platform)
	: impl(std::make_shared<Impl>(io, config, metrics, jwks, platform, *this))
{
}

RelayServer::~RelayServer() = default;

std::uint16_t RelayServer::start()
{
	return impl->start();
}

void RelayServer::beginDrain()
{
	if (impl->draining)
		return;
	impl->draining = true;
	impl->metrics.draining = true;
	logLine("info", "Draining: " + std::to_string(impl->matches.size()) + " match(es) in progress; no new matches");
	if (impl->config.drainTimeoutSeconds > 0)
	{
		impl->drainTimer.expires_after(std::chrono::seconds(impl->config.drainTimeoutSeconds));
		impl->drainTimer.async_wait([weak = std::weak_ptr<Impl>(impl)](boost::system::error_code ec) {
			auto self = weak.lock();
			if (!ec && self)
			{
				logLine("warning", "Drain timeout: aborting the remaining matches");
				self->owner.abortAll();
			}
		});
	}
	impl->checkDrained();
}

void RelayServer::abortAll()
{
	impl->draining = true;
	impl->metrics.draining = true;
	auto matches = impl->matches;
	for (auto& m : matches)
		m.second->abortNow();
	impl->checkDrained();
}

void RelayServer::stopListening()
{
	boost::system::error_code ignored;
	impl->acceptor.close(ignored);
}

bool RelayServer::draining() const
{
	return impl->draining;
}

LoadSnapshot RelayServer::snapshot() const
{
	return impl->snapshot();
}
}
