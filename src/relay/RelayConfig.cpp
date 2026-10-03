// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "RelayConfig.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

extern char** environ;

namespace Relay
{
namespace
{
	class Reader
	{
	public:
		explicit Reader(const std::map<std::string, std::string>& values) : values(values) {}

		bool has(const std::string& name) const
		{
			auto it = values.find(name);
			return it != values.end() && !it->second.empty();
		}
		std::string text(const std::string& name, const std::string& fallback) const
		{
			auto it = values.find(name);
			return it == values.end() || it->second.empty() ? fallback : it->second;
		}
		unsigned long number(const std::string& name, unsigned long fallback, unsigned long lo, unsigned long hi) const
		{
			if (!has(name))
				return fallback;
			const std::string v = values.at(name);
			if (v.find_first_not_of("0123456789") != std::string::npos || v.size() > 12)
				throw std::invalid_argument(name + " must be a non-negative integer");
			const unsigned long n = std::stoul(v);
			if (n < lo || n > hi)
				throw std::invalid_argument(name + " must be between " + std::to_string(lo) + " and " + std::to_string(hi));
			return n;
		}
		std::vector<std::string> list(const std::string& name) const
		{
			std::vector<std::string> out;
			std::stringstream in(text(name, ""));
			std::string item;
			while (std::getline(in, item, ','))
			{
				const auto b = item.find_first_not_of(" \t");
				const auto e = item.find_last_not_of(" \t");
				if (b != std::string::npos)
					out.push_back(item.substr(b, e - b + 1));
			}
			return out;
		}
		/// NAME, or the contents of the file named by NAME_FILE (trailing newline removed).
		std::string secret(const std::string& name) const
		{
			if (has(name + "_FILE"))
			{
				std::ifstream in(values.at(name + "_FILE"), std::ios::binary);
				if (!in)
					throw std::invalid_argument("Cannot read " + name + "_FILE");
				std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
					s.pop_back();
				return s;
			}
			return text(name, "");
		}

	private:
		const std::map<std::string, std::string>& values;
	};

	bool validId(const std::string& s, std::size_t max)
	{
		if (s.empty() || s.size() > max)
			return false;
		for (char c : s)
			if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-'))
				return false;
		return true;
	}

	bool validRegion(const std::string& s)
	{
		if (s.empty() || s.size() > 32)
			return false;
		for (std::size_t i = 0; i < s.size(); ++i)
		{
			const char c = s[i];
			const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
			if (!(alnum || (i > 0 && c == '-')))
				return false;
		}
		return true;
	}

	std::string defaultRelayId()
	{
		char host[256] = {};
		if (gethostname(host, sizeof(host) - 1) != 0)
			return "relay";
		std::string id;
		for (const char* p = host; *p && id.size() < 64; ++p)
		{
			const char c = *p;
			id += (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-') ? c : '-';
		}
		return id.empty() ? "relay" : id;
	}
}

RelayConfig RelayConfig::fromMap(const std::map<std::string, std::string>& values)
{
	Reader r(values);
	RelayConfig c;
	c.bindAddress = r.text("GLOB2_RELAY_BIND", c.bindAddress);
	c.port = static_cast<std::uint16_t>(r.number("GLOB2_RELAY_PORT", c.port, 0, 65535));
	c.route = r.text("GLOB2_RELAY_ROUTE", c.route);
	if (c.route.empty() || c.route[0] != '/')
		throw std::invalid_argument("GLOB2_RELAY_ROUTE must start with /");
	c.tlsCertificateFile = r.text("GLOB2_RELAY_TLS_CERT", "");
	c.tlsKeyFile = r.text("GLOB2_RELAY_TLS_KEY", "");
	if (c.tlsCertificateFile.empty() != c.tlsKeyFile.empty())
		throw std::invalid_argument("Set both GLOB2_RELAY_TLS_CERT and GLOB2_RELAY_TLS_KEY, or neither");
	c.allowedOrigins = r.list("GLOB2_RELAY_ALLOWED_ORIGINS");
	c.trustedProxies = r.list("GLOB2_RELAY_TRUSTED_PROXIES");
	c.metricsToken = r.secret("GLOB2_RELAY_METRICS_TOKEN");

	c.maxConnections = r.number("GLOB2_RELAY_MAX_CONNECTIONS", c.maxConnections, 1, 1000000);
	c.maxConnectionsPerAddress =
		r.number("GLOB2_RELAY_MAX_CONNECTIONS_PER_ADDRESS", c.maxConnectionsPerAddress, 1, 1000000);
	c.maxMatches = r.number("GLOB2_RELAY_MAX_MATCHES", c.maxMatches, 1, 1000000);
	c.helloTimeoutSeconds = static_cast<unsigned>(r.number("GLOB2_RELAY_HELLO_TIMEOUT_SECONDS", c.helloTimeoutSeconds, 1, 600));
	c.framesPerSecond = static_cast<unsigned>(r.number("GLOB2_RELAY_FRAMES_PER_SECOND", c.framesPerSecond, 1, 100000));
	c.frameBurst = static_cast<unsigned>(r.number("GLOB2_RELAY_FRAME_BURST", c.frameBurst, 1, 1000000));
	c.maxOutgoingBytes = r.number("GLOB2_RELAY_MAX_OUTGOING_BYTES", c.maxOutgoingBytes, 65536, 1ul << 32);

	c.graceSeconds = static_cast<unsigned>(r.number("GLOB2_RELAY_GRACE_SECONDS", c.graceSeconds, 1, 86400));
	c.drainTimeoutSeconds =
		static_cast<unsigned>(r.number("GLOB2_RELAY_DRAIN_TIMEOUT_SECONDS", c.drainTimeoutSeconds, 0, 7 * 86400));

	c.jwksFile = r.text("GLOB2_RELAY_JWKS_FILE", "");
	c.jwksUrl = r.text("GLOB2_RELAY_JWKS_URL", "");
	c.jwksRefreshSeconds = static_cast<unsigned>(r.number("GLOB2_RELAY_JWKS_REFRESH_SECONDS", c.jwksRefreshSeconds, 1, 86400));
	c.jwksMinRefreshSeconds =
		static_cast<unsigned>(r.number("GLOB2_RELAY_JWKS_MIN_REFRESH_SECONDS", c.jwksMinRefreshSeconds, 0, 3600));
	c.leewaySeconds = static_cast<std::int64_t>(r.number("GLOB2_RELAY_TICKET_LEEWAY_SECONDS", 30, 0, 600));
	c.issuer = r.text("GLOB2_RELAY_TICKET_ISSUER", "");

	c.platformUrl = r.text("GLOB2_RELAY_PLATFORM_URL", "");
	while (!c.platformUrl.empty() && c.platformUrl.back() == '/')
		c.platformUrl.pop_back();
	c.platformCaFile = r.text("GLOB2_RELAY_PLATFORM_CA", "");
	c.relayKey = r.secret("GLOB2_RELAY_KEY");
	c.relayId = r.text("GLOB2_RELAY_ID", defaultRelayId());
	if (!validId(c.relayId, 64))
		throw std::invalid_argument("GLOB2_RELAY_ID must be 1-64 characters of A-Z a-z 0-9 . _ -");
	c.publicUrl = r.text("GLOB2_RELAY_PUBLIC_URL", "");
	c.region = r.text("GLOB2_RELAY_REGION", c.region);
	if (!validRegion(c.region))
		throw std::invalid_argument("GLOB2_RELAY_REGION must match ^[a-z0-9][a-z0-9-]{0,31}$");
	c.spoolDirectory = r.text("GLOB2_RELAY_SPOOL_DIR", "");
	c.uploadAttempts = static_cast<unsigned>(r.number("GLOB2_RELAY_UPLOAD_ATTEMPTS", c.uploadAttempts, 1, 100));

	if (c.jwksFile.empty() && c.jwksUrl.empty() && c.platformUrl.empty())
		throw std::invalid_argument("Set GLOB2_RELAY_JWKS_FILE, GLOB2_RELAY_JWKS_URL or GLOB2_RELAY_PLATFORM_URL");
	if (!c.platformUrl.empty() && c.publicUrl.empty())
		throw std::invalid_argument("GLOB2_RELAY_PUBLIC_URL is required with GLOB2_RELAY_PLATFORM_URL");
	if (!c.platformUrl.empty() && c.relayKey.empty())
		throw std::invalid_argument("GLOB2_RELAY_KEY (or GLOB2_RELAY_KEY_FILE) is required with GLOB2_RELAY_PLATFORM_URL");
	return c;
}

RelayConfig RelayConfig::fromEnvironment()
{
	std::map<std::string, std::string> values;
	for (char** e = environ; e && *e; ++e)
	{
		const std::string entry(*e);
		const auto eq = entry.find('=');
		if (eq != std::string::npos && entry.compare(0, 12, "GLOB2_RELAY_") == 0)
			values[entry.substr(0, eq)] = entry.substr(eq + 1);
	}
	return fromMap(values);
}
}
