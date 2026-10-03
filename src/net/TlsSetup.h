// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// TLS context and server-verification setup shared by the native WebSocket
// transport (WssTransport) and HTTPS client (HttpFetch), so both trust exactly
// the same certificates.

#include "NetTransport.h"
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#if defined(GLOB2_MOBILE) || defined(__APPLE__) || defined(_WIN32)
#include "mobile/CertificateTrust.h"
#endif
#include <boost/asio/buffer.hpp>
#include <boost/asio/ssl.hpp>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

namespace NetTls
{
// TLS 1.2 minimum, an optional local identity, and (when loadTrust) the
// configured CA or the default verify paths.
inline void configure(boost::asio::ssl::context &context, const NetTlsConfig &config,
					  bool loadTrust = true)
{
	namespace asio = boost::asio;
	namespace ssl = asio::ssl;
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

inline std::shared_ptr<boost::asio::ssl::context>
contextFor(boost::asio::ssl::context::method method, const NetTlsConfig &config,
		   bool loadTrust = true)
{
	auto context = std::make_shared<boost::asio::ssl::context>(method);
	configure(*context, config, loadTrust);
	return context;
}

// Client trust as makeNetTransport resolves it: SSL_CERT_FILE names a CA
// bundle when the configuration supplies none.
inline NetTlsConfig withEnvironmentTrust(NetTlsConfig trust)
{
	if (trust.caFile.empty() && trust.caPem.empty())
	{
		const char *ca = std::getenv("SSL_CERT_FILE");
		if (ca && *ca)
			trust.caFile = ca;
	}
	return trust;
}

// Requires a valid server chain for `host`. Without a configured CA, Apple,
// mobile and Windows hosts verify through the platform trust store. `host` must
// outlive the connection: the platform verifier keeps a pointer to it.
// Certificate pinning (WssTransport LAN URLs) is handled by the caller instead.
template <class Stream>
void verifyServer(Stream &stream, boost::asio::ssl::context &context, const std::string &host,
				  const NetTlsConfig &config)
{
	namespace ssl = boost::asio::ssl;
#if defined(GLOB2_MOBILE) || defined(__APPLE__) || defined(_WIN32)
	if (config.caFile.empty() && config.caPem.empty())
		SSL_CTX_set_cert_verify_callback(context.native_handle(), MobileCertificateTrust::verify,
										 const_cast<std::string *>(&host));
	else
#else
	(void)context;
	(void)config;
#endif
		stream.set_verify_callback(ssl::host_name_verification(host));
}

// Sends `host` as the TLS server name indication.
template <class Stream> void serverName(Stream &stream, const std::string &host)
{
	if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
		throw std::runtime_error("Could not set TLS server name");
}
} // namespace NetTls
