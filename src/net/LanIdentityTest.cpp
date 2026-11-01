// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ScopedEnvironment.h"
#include "NetworkConfig.h"
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <iomanip>
#include <sstream>

TEST_SUITE("LanIdentity")
{
	TEST_CASE("self-signed LAN identities retain their name key trust and fingerprint through PEM")
	{
		glob2test::ScopedEnvironment address("GLOB2_LAN_ADDRESS", "127.0.0.1");
		const auto network = makeNetworkConfig(true);
		const auto &tls = network.lobby.tls;
		using Bio = std::unique_ptr<BIO, decltype(&BIO_free)>;
		Bio certificate(BIO_new_mem_buf(tls.certificatePem.data(),
			static_cast<int>(tls.certificatePem.size())), BIO_free);
		Bio key(BIO_new_mem_buf(tls.keyPem.data(), static_cast<int>(tls.keyPem.size())), BIO_free);
		REQUIRE(certificate);
		REQUIRE(key);
		std::unique_ptr<X509, decltype(&X509_free)> cert(
			PEM_read_bio_X509(certificate.get(), nullptr, nullptr, nullptr), X509_free);
		std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> privateKey(
			PEM_read_bio_PrivateKey(key.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
		REQUIRE(cert);
		REQUIRE(privateKey);
		const auto *subject = X509_get_subject_name(cert.get());
		const auto *issuer = X509_get_issuer_name(cert.get());
		REQUIRE(subject);
		REQUIRE(issuer);
		CHECK(X509_NAME_entry_count(subject) == 1);
		CHECK(X509_NAME_cmp(subject, issuer) == 0);
		char commonName[128]{};
		REQUIRE(X509_NAME_get_text_by_NID(subject, NID_commonName, commonName, sizeof(commonName)) > 0);
		CHECK(std::string(commonName) == "Globulation 2 LAN session");
		CHECK(X509_get_version(cert.get()) == 2);
		CHECK(ASN1_INTEGER_get(X509_get0_serialNumber(cert.get())) == 1);
		CHECK(X509_check_private_key(cert.get(), privateKey.get()) == 1);
		CHECK(X509_verify(cert.get(), privateKey.get()) == 1);
		CHECK(X509_check_ip_asc(cert.get(), "127.0.0.1", 0) == 1);
		CHECK(X509_check_ip_asc(cert.get(), "::1", 0) == 1);
		CHECK(X509_check_host(cert.get(), "localhost", 0, 0, nullptr) == 1);
		CHECK(tls.caPem == tls.certificatePem);
		CHECK(network.router.tls.certificatePem == tls.certificatePem);
		CHECK(network.router.tls.keyPem == tls.keyPem);
		CHECK(network.registration.tls.certificatePem == tls.certificatePem);
		CHECK(network.registration.tls.keyPem == tls.keyPem);
		CHECK(network.registration.tls.requireClientCertificate);
		unsigned char digest[EVP_MAX_MD_SIZE]{};
		unsigned size = 0;
		REQUIRE(X509_digest(cert.get(), EVP_sha256(), digest, &size) == 1);
		REQUIRE(size == 32);
		std::ostringstream fingerprint;
		fingerprint << std::hex << std::setfill('0');
		for (unsigned i = 0; i < size; ++i)
			fingerprint << std::setw(2) << static_cast<unsigned>(digest[i]);
		CHECK(NetEndpoint::parse(network.lobbyEndpoint).fingerprint == fingerprint.str());
		CHECK(NetEndpoint::parse(network.routerEndpoint).fingerprint == fingerprint.str());
	}

	TEST_CASE("a scoped IPv6 LAN address has an unscoped certificate identity")
	{
		glob2test::ScopedEnvironment address("GLOB2_LAN_ADDRESS", "fe80::1%5");
		auto network = makeNetworkConfig(true);
		const auto &certificate = network.lobby.tls.certificatePem;
		std::unique_ptr<BIO, decltype(&BIO_free)> bio(
			BIO_new_mem_buf(certificate.data(), static_cast<int>(certificate.size())), BIO_free);
		REQUIRE(bio);
		std::unique_ptr<X509, decltype(&X509_free)> cert(
			PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), X509_free);
		REQUIRE(cert);
		CHECK(X509_check_ip_asc(cert.get(), "fe80::1", 0) == 1);
		CHECK(X509_check_ip_asc(cert.get(), "127.0.0.1", 0) == 1);
		CHECK(network.lobbyEndpoint.find("[fe80::1%5]") != std::string::npos);
	}

}
