// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetTransport.h"
#include <cstdlib>
std::unique_ptr<NetTransport> makeWssTransport(const NetTlsConfig &);
std::unique_ptr<NetTransportListener> makeWssTransportListener(const NetListenConfig &);
#ifndef __EMSCRIPTEN__
std::unique_ptr<NetTransport> makeTcpTransport();
std::unique_ptr<NetTransportListener> makeTcpTransportListener(const NetListenConfig &);
namespace
{
class RoutedTransport final : public NetTransport
{
	NetTlsConfig tls;
	std::unique_ptr<NetTransport> selected;

  public:
	explicit RoutedTransport(NetTlsConfig config) : tls(std::move(config)) {}
	void open(const std::string &endpoint, uint16_t port) override
	{
		close();
		selected = endpoint.rfind("wss://", 0) == 0 ? makeWssTransport(tls) : makeTcpTransport();
		selected->open(endpoint, port);
	}
	void close() override
	{
		if (selected)
			selected->close();
		selected.reset();
	}
	State state() const override { return selected ? selected->state() : State::Closed; }
	bool send(std::vector<uint8_t> bytes) override
	{
		return selected && selected->send(std::move(bytes));
	}
	bool receive(std::vector<uint8_t> &bytes) override
	{
		return selected && selected->receive(bytes);
	}
	std::string peerAddress() const override
	{
		return selected ? selected->peerAddress() : std::string();
	}
	std::string error() const override { return selected ? selected->error() : std::string(); }
};
} // namespace
#endif
std::unique_ptr<NetTransport> makeNetTransport(const NetTlsConfig &tls)
{
	auto trust = tls;
	if (trust.caFile.empty() && trust.caPem.empty())
	{
		const char *ca = std::getenv("SSL_CERT_FILE");
		if (ca && *ca)
			trust.caFile = ca;
	}
#ifdef __EMSCRIPTEN__
	return makeWssTransport(trust);
#else
	return std::make_unique<RoutedTransport>(std::move(trust));
#endif
}

std::string configuredYogEndpoint(const std::string &defaultEndpoint)
{
	const char *endpoint = std::getenv("GLOB2_YOG_URL");
	return endpoint && *endpoint ? endpoint : defaultEndpoint;
}

std::unique_ptr<NetTransportListener> makeNetTransportListener(const NetListenConfig &config)
{
#ifndef __EMSCRIPTEN__
	if (config.protocol == NetListenConfig::Protocol::Tcp)
		return makeTcpTransportListener(config);
#endif
	return makeWssTransportListener(config);
}
