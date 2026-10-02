// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetTransport.h"
#include "TlsSetup.h"
#include <cstdlib>
std::unique_ptr<NetTransport> makeWssTransport(const NetTlsConfig &, NetMessageMode);
std::unique_ptr<NetTransport> makeNetTransport(const NetTlsConfig &tls, NetMessageMode mode)
{
	return makeWssTransport(NetTls::withEnvironmentTrust(tls), mode);
}

std::string configuredYogEndpoint(const std::string &defaultEndpoint)
{
	const char *endpoint = std::getenv("GLOB2_YOG_URL");
	return endpoint && *endpoint ? endpoint : defaultEndpoint;
}
