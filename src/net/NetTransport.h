// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct NetTlsConfig
{
	std::string certificateFile, keyFile, caFile;
	// Session-only LAN identities never need to be written to disk.
	std::string certificatePem, keyPem, caPem;
	bool requireClientCertificate = false;
};
// How a connection maps WebSocket messages, fixed when it is opened or accepted.
// Binary: an ordered byte stream of binary messages for NetConnection framing
// (send/receive). Text: whole UTF-8 text messages, one per sendText/receiveText,
// for JSON protocols. Messages of the other kind close the connection.
enum class NetMessageMode
{
	Binary,
	Text
};
struct NetListenConfig
{
	enum class Protocol { Wss, Tcp };
	Protocol protocol = Protocol::Wss;
	std::string bindAddress = "::", route = "/yog";
	uint16_t port = 7489;
	NetTlsConfig tls;
	std::vector<std::string> allowedOrigins, trustedProxyAddresses;
	size_t connectionLimit = 256;
	NetMessageMode messageMode = NetMessageMode::Binary;
};
struct NetEndpoint
{
	std::string host, authority, service, route, fingerprint;
	static NetEndpoint parse(const std::string &url);
};

// TCP and binary WebSocket chunks form an ordered byte stream. NetConnection owns framing.
// Text-mode (WebSocket only) connections exchange whole text messages instead.
class NetTransport
{
  public:
	enum class State
	{
		Closed,
		Connecting,
		Connected
	};
	static constexpr size_t queueLimit = 1024 * 1024;
	static constexpr size_t chunkLimit = 16 * 1024;
	// Largest text message either direction accepts (binary messages: 64 KiB).
	static constexpr size_t textMessageLimit = 256 * 1024;
	virtual ~NetTransport() = default;
	// The port parameter is retained for source compatibility; URLs own routing.
	virtual void open(const std::string &endpoint, uint16_t port = 0) = 0;
	virtual void close() = 0;
	virtual State state() const = 0;
	virtual bool send(std::vector<uint8_t> bytes) = 0;
	virtual bool receive(std::vector<uint8_t> &bytes) = 0;
	// Text mode only. Sends one text message; the caller supplies valid UTF-8
	// without NUL characters, at most textMessageLimit bytes. Returns false if
	// the message was not queued (wrong mode, not connected, too large, or the
	// output queue is full).
	virtual bool sendText(std::string)
	{
		return false;
	}
	// Text mode only. Takes the next whole received text message.
	virtual bool receiveText(std::string &)
	{
		return false;
	}
	virtual std::string peerAddress() const
	{
		return {};
	}
	virtual std::string error() const
	{
		return {};
	}
};
class NetTransportListener
{
  public:
	virtual ~NetTransportListener() = default;
	virtual std::unique_ptr<NetTransport> accept() = 0;
	virtual void close() = 0;
	virtual bool listening() const = 0;
};
std::unique_ptr<NetTransport> makeNetTransport(const NetTlsConfig &tls = {},
											   NetMessageMode mode = NetMessageMode::Binary);
std::unique_ptr<NetTransportListener> makeNetTransportListener(const NetListenConfig &config);

std::string configuredYogEndpoint(const std::string &defaultEndpoint);
