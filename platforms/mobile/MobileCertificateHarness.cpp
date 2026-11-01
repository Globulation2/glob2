// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "../mobile/CertificateTrust.h"
#include <openssl/pem.h>
#include <cstdlib>
#include <memory>
#include <string>
#ifndef GLOB2_APPLE_TRUST_TEST
namespace MobileCertificateTrust {
bool accepting = true;
unsigned calls = 0;
bool platformVerify(const Chain& chain, const std::string&) { ++calls; REQUIRE(!chain.empty()); return accepting; }
}
#endif
TEST_SUITE("MobileCertificate")
{
TEST_CASE("certificate trust and identity checks")
{
    using namespace MobileCertificateTrust;
    // An isolated self-signed certificate for mobile.test, as the old runner made.
    if (std::system("openssl version >/dev/null 2>&1") != 0)
    {
        MESSAGE("openssl is not on PATH; skipping");
        return;
    }
    glob2test::TempDir dir("certificate");
    const std::string certificate = (dir.path / "certificate.pem").string();
    const std::string command = "openssl req -x509 -newkey rsa:2048 -nodes -days 2 -keyout \"" + (dir.path / "key.pem").string()
        + "\" -out \"" + certificate + "\" -subj /CN=mobile.test -addext subjectAltName=DNS:mobile.test >/dev/null 2>&1";
    REQUIRE(std::system(command.c_str()) == 0);
    FILE* input = fopen(certificate.c_str(), "rb");
    REQUIRE(input != nullptr);
    std::unique_ptr<X509, decltype(&X509_free)> leaf(PEM_read_X509(input, nullptr, nullptr, nullptr), X509_free);
    auto* chain = sk_X509_new_null();
    while (auto* cert = PEM_read_X509(input, nullptr, nullptr, nullptr)) sk_X509_push(chain, cert);
    fclose(input);
    REQUIRE(leaf);
    auto* context = X509_STORE_CTX_new();
    X509_STORE_CTX_init(context, nullptr, leaf.get(), chain);
    std::string host = "mobile.test";
#ifdef GLOB2_APPLE_TRUST_TEST
    const bool expected = false;
    REQUIRE(bool(verify(context, &host)) == expected);
#else
    REQUIRE(verify(context, &host) == 1);
    REQUIRE(X509_STORE_CTX_get_error(context) == X509_V_OK);
    accepting = false;
    REQUIRE(verify(context, &host) == 0);
    REQUIRE(X509_STORE_CTX_get_error(context) == X509_V_ERR_APPLICATION_VERIFICATION);
    accepting = true;
    host = "wrong.example.invalid";
    const unsigned before = calls;
    REQUIRE((verify(context, &host) == 0 && calls == before));
    REQUIRE(X509_STORE_CTX_get_error(context) == X509_V_ERR_HOSTNAME_MISMATCH);
    host = "mobile.test";
    for (int i = 0; i < 17; ++i) sk_X509_push(chain, X509_dup(leaf.get()));
    REQUIRE(verify(context, &host) == 0);
    host.clear(); REQUIRE(verify(context, &host) == 0);
    REQUIRE(verify(context, nullptr) == 0);
#endif
    X509_STORE_CTX_free(context);
    sk_X509_pop_free(chain, X509_free);
}
}
