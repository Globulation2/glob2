// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ScopedEnvironment.h"
#include "NetworkConfig.h"
#include <openssl/pem.h>
#include <openssl/x509v3.h>

TEST_SUITE("LanIdentity")
{
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
