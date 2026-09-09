// SPDX-License-Identifier: GPL-3.0-or-later
#include "../mobile/CertificateTrust.h"
#include <openssl/pem.h>
#include <cassert>
#include <memory>
#include <iostream>
#ifndef GLOB2_APPLE_TRUST_TEST
namespace MobileCertificateTrust {
bool accepting = true;
unsigned calls = 0;
bool platformVerify(const Chain& chain, const std::string&) { ++calls; assert(!chain.empty()); return accepting; }
}
#endif
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    using namespace MobileCertificateTrust;
    FILE* input = fopen(argv[1], "rb");
    if (!input) return 2;
    std::unique_ptr<X509, decltype(&X509_free)> leaf(PEM_read_X509(input, nullptr, nullptr, nullptr), X509_free);
    auto* chain = sk_X509_new_null();
    while (auto* cert = PEM_read_X509(input, nullptr, nullptr, nullptr)) sk_X509_push(chain, cert);
    fclose(input);
    if (!leaf) return 2;
    auto* context = X509_STORE_CTX_new();
    X509_STORE_CTX_init(context, nullptr, leaf.get(), chain);
    std::string host = argv[2];
#ifdef GLOB2_APPLE_TRUST_TEST
    const bool expected = argc > 3 && std::string(argv[3]) == "trusted";
    assert(bool(verify(context, &host)) == expected);
#else
    assert(verify(context, &host) == 1);
    assert(X509_STORE_CTX_get_error(context) == X509_V_OK);
    accepting = false;
    assert(verify(context, &host) == 0);
    assert(X509_STORE_CTX_get_error(context) == X509_V_ERR_APPLICATION_VERIFICATION);
    accepting = true;
    host = "wrong.example.invalid";
    const unsigned before = calls;
    assert(verify(context, &host) == 0 && calls == before);
    assert(X509_STORE_CTX_get_error(context) == X509_V_ERR_HOSTNAME_MISMATCH);
    host = argv[2];
    for (int i = 0; i < 17; ++i) sk_X509_push(chain, X509_dup(leaf.get()));
    assert(verify(context, &host) == 0);
    host.clear(); assert(verify(context, &host) == 0);
    assert(verify(context, nullptr) == 0);
#endif
    X509_STORE_CTX_free(context);
    sk_X509_pop_free(chain, X509_free);
    std::cout << "PASS: certificate trust and identity checks\n";
}
