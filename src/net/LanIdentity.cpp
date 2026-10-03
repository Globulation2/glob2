// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetworkConfig.h"
#include <boost/asio.hpp>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/x509v3.h>
#include <set>
#include <stdexcept>
#include <cstdlib>
#ifndef _WIN32
#include <ifaddrs.h>
#include <netdb.h>
#endif
namespace
{
std::string hexadecimal(const unsigned char *data, size_t size)
{
	std::string result;
	for (size_t i = 0; i < size; ++i)
	{
		result += "0123456789abcdef"[data[i] >> 4];
		result += "0123456789abcdef"[data[i] & 15];
	}
	return result;
}
std::string pem(BIO *bio)
{
	char *bytes = nullptr;
	auto size = BIO_get_mem_data(bio, &bytes);
	if (size <= 0)
		throw std::runtime_error("LAN identity serialization failed");
	return std::string(bytes, size);
}
} // namespace
void provisionLanIdentity(NetworkConfig &config)
{
	std::set<std::string> addresses{"127.0.0.1", "::1"};
#ifndef _WIN32
	ifaddrs *interfaces = nullptr;
	if (getifaddrs(&interfaces) == 0)
	{
		for (auto *i = interfaces; i; i = i->ifa_next)
		{
			if (!i->ifa_addr ||
				(i->ifa_addr->sa_family != AF_INET && i->ifa_addr->sa_family != AF_INET6))
				continue;
			char host[NI_MAXHOST];
			const auto size =
				i->ifa_addr->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
			if (getnameinfo(i->ifa_addr, size, host, sizeof(host), nullptr, 0, NI_NUMERICHOST) ==
					0 &&
				std::string(host).find('%') == std::string::npos)
				addresses.insert(host);
		}
		freeifaddrs(interfaces);
	}
#else
	boost::asio::io_context io;
	boost::asio::ip::tcp::resolver resolver(io);
	boost::system::error_code error;
	for (const auto &entry : resolver.resolve(boost::asio::ip::host_name(), "0", error))
	{
		const auto address = entry.endpoint().address().to_string();
		// An IPv6 zone is local interface metadata, not an X.509 IP address.
		// Match the Unix interface enumeration above and omit scoped entries.
		if (address.find('%') == std::string::npos)
			addresses.insert(address);
	}
#endif
	std::string host = "127.0.0.1";
	for (const auto &address : addresses)
		if (address.find(':') == std::string::npos && address.rfind("127.", 0) != 0)
		{
			host = address;
			break;
		}
	if (const auto *configured = std::getenv("GLOB2_LAN_ADDRESS"))
	{
		host = configured;
		boost::asio::ip::make_address(
			host); // Only IP literals, never attacker-controlled SAN syntax.
		addresses.insert(host);
	}
	using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
	using Cert = std::unique_ptr<X509, decltype(&X509_free)>;
	using Bio = std::unique_ptr<BIO, decltype(&BIO_free)>;
	Key key(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"), EVP_PKEY_free);
	Cert cert(X509_new(), X509_free);
	if (!key || !cert)
		throw std::runtime_error("LAN identity generation failed");
	unsigned char random[16];
	if (RAND_bytes(random, sizeof(random)) != 1)
		throw std::runtime_error("LAN random identity failed");
	config.discoveryId = hexadecimal(random, sizeof(random));
	X509_set_version(cert.get(), 2);
	ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
	X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60);
	X509_gmtime_adj(X509_getm_notAfter(cert.get()), 7 * 24 * 60 * 60);
	X509_set_pubkey(cert.get(), key.get());
	auto *name = X509_get_subject_name(cert.get());
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
							   reinterpret_cast<const unsigned char *>("Globulation 2 LAN session"),
							   -1, -1, 0);
	X509_set_issuer_name(cert.get(), name);
	X509V3_CTX context{};
	X509V3_set_ctx(&context, cert.get(), cert.get(), nullptr, nullptr, 0);
	std::string san = "DNS:localhost";
	for (const auto &address : addresses)
	{
		auto ip = boost::asio::ip::make_address(address);
		if (ip.is_v6())
		{
			// Interface scopes select a local route, but are not part of the
			// address bytes encoded in a certificate's IP subject alternative name.
			auto ipv6 = ip.to_v6();
			ipv6.scope_id(0);
			ip = ipv6;
		}
		san += ",IP:" + ip.to_string();
	}
	for (const auto &extension :
		 {std::make_pair(NID_basic_constraints, std::string("critical,CA:TRUE")),
		  std::make_pair(NID_key_usage, std::string("critical,digitalSignature,keyCertSign")),
		  std::make_pair(NID_ext_key_usage, std::string("serverAuth,clientAuth")),
		  std::make_pair(NID_subject_alt_name, san)})
	{
		auto *value =
			X509V3_EXT_conf_nid(nullptr, &context, extension.first, extension.second.c_str());
		if (!value)
		{
			char error[256];
			ERR_error_string_n(ERR_get_error(), error, sizeof(error));
			throw std::runtime_error(std::string("LAN identity extension ") +
				OBJ_nid2sn(extension.first) + " failed: " + error);
		}
		const auto result = X509_add_ext(cert.get(), value, -1);
		X509_EXTENSION_free(value);
		if (!result)
			throw std::runtime_error("LAN identity extension failed");
	}
	if (!X509_sign(cert.get(), key.get(), EVP_sha256()))
		throw std::runtime_error("LAN identity signing failed");
	Bio certificate(BIO_new(BIO_s_mem()), BIO_free), privateKey(BIO_new(BIO_s_mem()), BIO_free);
	if (!certificate || !privateKey || !PEM_write_bio_X509(certificate.get(), cert.get()) ||
		!PEM_write_bio_PrivateKey(privateKey.get(), key.get(), nullptr, nullptr, 0, nullptr,
								  nullptr))
		throw std::runtime_error("LAN identity serialization failed");
	NetTlsConfig tls;
	tls.certificatePem = pem(certificate.get());
	tls.keyPem = pem(privateKey.get());
	tls.caPem = tls.certificatePem;
	config.lobby.tls = config.router.tls = config.registration.tls = tls;
	config.registration.tls.requireClientCertificate = true;
	config.registration.bindAddress = "127.0.0.1";
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned size = 0;
	if (!X509_digest(cert.get(), EVP_sha256(), digest, &size) || size != 32)
		throw std::runtime_error("LAN identity fingerprint failed");
	if (host.find(':') != std::string::npos)
		config.lobby.bindAddress = config.router.bindAddress = "::";
	const auto authority = host.find(':') == std::string::npos ? host : "[" + host + "]";
	const auto pin = "#sha256=" + hexadecimal(digest, size);
	config.lobbyEndpoint = "wss://" + authority + ":7489/yog" + pin;
	config.routerEndpoint = "wss://" + authority + ":7491/router" + pin;
	config.registrationEndpoint = "wss://localhost:7490/register";
}
