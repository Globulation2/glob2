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
struct NetListenConfig
{
	std::string bindAddress = "0.0.0.0", route = "/yog";
	uint16_t port = 7489;
	NetTlsConfig tls;
	std::vector<std::string> allowedOrigins, trustedProxyAddresses;
	size_t connectionLimit = 256;
};
struct NetEndpoint
{
	std::string host, authority, service, route, fingerprint;
	static NetEndpoint parse(const std::string &url);
};

// Binary WebSocket chunks form an ordered byte stream. NetConnection owns framing.
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
	virtual ~NetTransport() = default;
	// The port parameter is retained for source compatibility; URLs own routing.
	virtual void open(const std::string &endpoint, uint16_t port = 0) = 0;
	virtual void close() = 0;
	virtual State state() const = 0;
	virtual bool send(std::vector<uint8_t> bytes) = 0;
	virtual bool receive(std::vector<uint8_t> &bytes) = 0;
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
std::unique_ptr<NetTransport> makeNetTransport(const NetTlsConfig &tls = {});
std::unique_ptr<NetTransportListener> makeNetTransportListener(const NetListenConfig &config);

std::string configuredYogEndpoint(const std::string &defaultEndpoint);
