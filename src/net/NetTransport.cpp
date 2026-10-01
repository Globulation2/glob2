// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetTransport.h"
#include <cstdlib>
std::unique_ptr<NetTransport> makeWssTransport(const NetTlsConfig &);
std::unique_ptr<NetTransport> makeNetTransport(const NetTlsConfig &tls)
{
	auto trust = tls;
	if (trust.caFile.empty() && trust.caPem.empty())
	{
		const char *ca = std::getenv("SSL_CERT_FILE");
		if (ca && *ca)
			trust.caFile = ca;
	}
	return makeWssTransport(trust);
}

std::string configuredYogEndpoint(const std::string &defaultEndpoint)
{
	const char *endpoint = std::getenv("GLOB2_YOG_URL");
	return endpoint && *endpoint ? endpoint : defaultEndpoint;
}
