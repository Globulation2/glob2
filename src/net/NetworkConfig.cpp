// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetworkConfig.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <sstream>

NetEndpoint NetEndpoint::parse(const std::string &url)
{
	if (url.rfind("wss://", 0) != 0 || url.size() > 2048 ||
		std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= 32 || c >= 127; }) ||
		url.find_first_of("@?\\ \t\r\n") != std::string::npos)
		throw std::invalid_argument(
			"Expected a secure WebSocket URL without credentials or query parameters");
	NetEndpoint e;
	auto rest = url.substr(6);
	auto fragment = rest.find('#');
	if (fragment != std::string::npos)
	{
		const auto pin = rest.substr(fragment);
		if (pin.rfind("#sha256=", 0) != 0 || pin.size() != 72)
			throw std::invalid_argument("Invalid LAN certificate fingerprint");
		e.fingerprint = pin.substr(8);
		if (e.fingerprint.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
			throw std::invalid_argument("Invalid LAN certificate fingerprint");
		std::transform(e.fingerprint.begin(), e.fingerprint.end(), e.fingerprint.begin(),
					   [](unsigned char c) { return std::tolower(c); });
		rest.resize(fragment);
	}
	auto slash = rest.find('/');
	if (slash == std::string::npos)
		throw std::invalid_argument("WebSocket URL requires an explicit route");
	e.authority = rest.substr(0, slash);
	e.route = rest.substr(slash);
	// A relay's public URL is /relay/<relay id> behind the instance's proxy, or
	// /relay when a client connects to the relay directly.
	const auto relayRoute = [](const std::string &route) {
		if (route == "/relay")
			return true;
		if (route.rfind("/relay/", 0) != 0)
			return false;
		const auto id = route.substr(7);
		return !id.empty() && id.size() <= 63 && id.front() != '-' &&
			   id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos;
	};
	if (e.route != "/yog" && e.route != "/router" && e.route != "/register" &&
		e.route != "/realtime" && !relayRoute(e.route))
		throw std::invalid_argument("Unknown WebSocket route");
	e.service = "443";
	if (!e.authority.empty() && e.authority.front() == '[')
	{
		const auto end = e.authority.find(']');
		if (end == std::string::npos)
			throw std::invalid_argument("Invalid IPv6 endpoint");
		e.host = e.authority.substr(1, end - 1);
		if (end + 1 < e.authority.size())
		{
			if (e.authority[end + 1] != ':')
				throw std::invalid_argument("Invalid endpoint port");
			e.service = e.authority.substr(end + 2);
		}
	}
	else
	{
		auto colon = e.authority.find(':');
		e.host = e.authority.substr(0, colon);
		if (colon != std::string::npos)
			e.service = e.authority.substr(colon + 1);
	}
	if (e.host.empty() || e.host.size() > 253 || e.service.empty() || e.service.size() > 5 ||
		e.service.find_first_not_of("0123456789") != std::string::npos ||
		std::stoul(e.service) == 0 || std::stoul(e.service) > 65535 ||
		e.host.find_first_of("[]#") != std::string::npos)
		throw std::invalid_argument("Invalid WebSocket host or port");
	return e;
}
namespace
{
std::string setting(const char *name, const std::string &fallback = {})
{
	const char *v = std::getenv(name);
	return v && *v ? v : fallback;
}
unsigned number(const char *name, unsigned fallback, unsigned maximum)
{
	auto text = setting(name, std::to_string(fallback));
	if (text.empty() || text.size() > 9 ||
		text.find_first_not_of("0123456789") != std::string::npos)
		throw std::invalid_argument(std::string("Invalid ") + name);
	auto n = std::stoul(text);
	if (!n || n > maximum)
		throw std::invalid_argument(std::string("Invalid ") + name);
	return n;
}
std::vector<std::string> allowedOrigins()
{
	std::vector<std::string> origins;
	std::istringstream input(setting("GLOB2_ALLOWED_ORIGINS"));
	std::string origin;
	while (std::getline(input, origin, ','))
	{
		const bool https = origin.rfind("https://", 0) == 0;
		const bool loopback =
			origin.rfind("http://localhost:", 0) == 0 || origin.rfind("http://127.0.0.1:", 0) == 0;
		if (!https && !loopback)
			throw std::invalid_argument("Origins must be HTTPS (or loopback development) origins");
		const auto authority = origin.substr(origin.find("://") + 3);
		if (authority.find_first_of("/# \r\n\t") != std::string::npos)
			throw std::invalid_argument("Origin cannot include a path");
		NetEndpoint::parse("wss://" + authority + "/yog");
		origins.push_back(origin);
	}
	return origins;
}

} // namespace
#ifndef __EMSCRIPTEN__
void provisionLanIdentity(NetworkConfig &);
#endif
NetworkConfig makeNetworkConfig(bool lan, bool routerRole)
{
	NetworkConfig c;
	c.lan = lan;
	c.router.port = 7491;
	c.router.route = "/router";
	c.registration.port = 7490;
	c.registration.route = "/register";
	c.registration.tls.requireClientCertificate = true;
	c.controlPort = routerRole ? 7493 : 7492;
	if (lan)
	{
#ifdef __EMSCRIPTEN__
		throw std::runtime_error("LAN hosting requires a native application");
#else
		provisionLanIdentity(c);
		c.lobby.allowedOrigins = c.router.allowedOrigins = allowedOrigins();
#endif
		return c;
	}
	NetTlsConfig tls;
	tls.certificateFile = setting("GLOB2_TLS_CERT");
	tls.keyFile = setting("GLOB2_TLS_KEY");
	tls.caFile = setting("GLOB2_TLS_CA");
	if (tls.certificateFile.empty() || tls.keyFile.empty() || tls.caFile.empty())
		throw std::invalid_argument("GLOB2_TLS_CERT, GLOB2_TLS_KEY, and GLOB2_TLS_CA are required");
	auto origins = allowedOrigins();
	for (auto *listener : {&c.lobby, &c.router, &c.registration})
	{
		listener->tls = tls;
		listener->bindAddress = setting("GLOB2_BIND_ADDRESS", "::");
		listener->connectionLimit = number("GLOB2_CONNECTION_LIMIT", 256, 65535);
		listener->allowedOrigins = origins;
	}
	c.registration.tls.requireClientCertificate = true;
	std::istringstream proxies(setting("GLOB2_TRUSTED_PROXY_ADDRESSES"));
	std::string proxy;
	while (std::getline(proxies, proxy, ','))
	{
		c.lobby.trustedProxyAddresses.push_back(proxy);
		c.router.trustedProxyAddresses.push_back(proxy);
	}
	c.lobby.port = number("GLOB2_LOBBY_PORT", 7489, 65535);
	c.router.port = number("GLOB2_ROUTER_PORT", 7491, 65535);
	c.registration.port = number("GLOB2_REGISTRATION_PORT", 7490, 65535);
	c.lobbyEndpoint = setting("GLOB2_PUBLIC_LOBBY_ENDPOINT", "wss://yog.globulation2.org/yog");
	c.routerEndpoint = setting("GLOB2_PUBLIC_ROUTER_ENDPOINT", "wss://yog.globulation2.org/router");
	c.registrationEndpoint =
		setting("GLOB2_REGISTRATION_ENDPOINT", "wss://localhost:7490/register");
	if (NetEndpoint::parse(c.lobbyEndpoint).route != "/yog" ||
		NetEndpoint::parse(c.routerEndpoint).route != "/router" ||
		NetEndpoint::parse(c.registrationEndpoint).route != "/register")
		throw std::invalid_argument("Endpoint route does not match service");
	c.controlBind = setting("GLOB2_CONTROL_BIND", "127.0.0.1");
	c.controlPort = number("GLOB2_CONTROL_PORT", c.controlPort, 65535);
	c.drainSeconds = number("GLOB2_DRAIN_SECONDS", 1800, 86400);
	return c;
}
