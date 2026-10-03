// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetTransport.h"
#include "TlsSetup.h"
#include <cstdlib>
std::unique_ptr<NetTransport> makeWssTransport(const NetTlsConfig &, NetMessageMode);
std::unique_ptr<NetTransportListener> makeWssTransportListener(const NetListenConfig &);
#ifndef __EMSCRIPTEN__
std::unique_ptr<NetTransport> makeTcpTransport();
std::unique_ptr<NetTransportListener> makeTcpTransportListener(const NetListenConfig &);
namespace
{
// Picks the transport from the URL when the connection opens: wss:// gets
// WebSocket, anything else plain TCP. Text mode exists only on WebSocket, so a
// text-mode connection always uses it (and reports a bad URL itself).
class RoutedTransport final : public NetTransport
{
	NetTlsConfig tls;
	NetMessageMode mode;
	std::unique_ptr<NetTransport> selected;

  public:
	RoutedTransport(NetTlsConfig config, NetMessageMode mode) : tls(std::move(config)), mode(mode) {}
	void open(const std::string &endpoint, uint16_t port) override
	{
		close();
		const bool wss = endpoint.rfind("wss://", 0) == 0 || mode == NetMessageMode::Text;
		selected = wss ? makeWssTransport(tls, mode) : makeTcpTransport();
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
	bool sendText(std::string text) override
	{
		return selected && selected->sendText(std::move(text));
	}
	bool receiveText(std::string &text) override
	{
		return selected && selected->receiveText(text);
	}
	std::string peerAddress() const override
	{
		return selected ? selected->peerAddress() : std::string();
	}
	size_t pendingOutgoing() const override
	{
		return selected ? selected->pendingOutgoing() : 0;
	}
	std::string error() const override { return selected ? selected->error() : std::string(); }
};
} // namespace
#endif
std::unique_ptr<NetTransport> makeNetTransport(const NetTlsConfig &tls, NetMessageMode mode)
{
	auto trust = NetTls::withEnvironmentTrust(tls);
#ifdef __EMSCRIPTEN__
	return makeWssTransport(trust, mode);
#else
	return std::make_unique<RoutedTransport>(std::move(trust), mode);
#endif
}

std::unique_ptr<NetTransportListener> makeNetTransportListener(const NetListenConfig &config)
{
#ifndef __EMSCRIPTEN__
	if (config.protocol == NetListenConfig::Protocol::Tcp)
		return makeTcpTransportListener(config);
#endif
	return makeWssTransportListener(config);
}
