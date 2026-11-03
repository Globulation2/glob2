// SPDX-License-Identifier: GPL-3.0-or-later
#include "CertificateTrust.h"
#include <openssl/x509v3.h>
#include <memory>

int MobileCertificateTrust::verify(X509_STORE_CTX* context, void* hostname) noexcept {
    if (!context) return 0;
    X509_STORE_CTX_set_error(context, X509_V_ERR_APPLICATION_VERIFICATION);
    try {
        if (!hostname) return 0;
        const auto& host = *static_cast<const std::string*>(hostname);
        if (host.empty() || host.size() > 253 || host.find('\0') != std::string::npos) return 0;
        for (unsigned char c : host) if (c < 33 || c > 126) return 0;
        auto* leaf = X509_STORE_CTX_get0_cert(context);
        if (!leaf) return 0;
        std::unique_ptr<ASN1_OCTET_STRING, decltype(&ASN1_OCTET_STRING_free)> ip(a2i_IPADDRESS(host.c_str()), ASN1_OCTET_STRING_free);
        const int identity = ip ? X509_check_ip_asc(leaf, host.c_str(), 0) :
            X509_check_host(leaf, host.data(), host.size(), X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS, nullptr);
        if (identity != 1) { X509_STORE_CTX_set_error(context, X509_V_ERR_HOSTNAME_MISMATCH); return 0; }
        Chain chain;
        size_t total = 0;
        auto append = [&](X509* certificate) {
            const int size = i2d_X509(certificate, nullptr);
            if (size <= 0 || size > 64 * 1024 || chain.size() >= 16 || static_cast<size_t>(size) > 128 * 1024 - total) return false;
            chain.emplace_back(size);
            auto* out = chain.back().data();
            if (i2d_X509(certificate, &out) != size) return false;
            total += size;
            return true;
        };
        if (!append(leaf)) return 0;
        auto* supplied = X509_STORE_CTX_get0_untrusted(context);
        if (sk_X509_num(supplied) > 16) return 0;
        for (int i = 0; i < sk_X509_num(supplied); ++i) {
            auto* certificate = sk_X509_value(supplied, i);
            if (certificate != leaf && X509_cmp(certificate, leaf) != 0 && !append(certificate)) return 0;
        }
        if (!platformVerify(chain, host)) return 0;
        X509_STORE_CTX_set_error(context, X509_V_OK);
        return 1;
    } catch (...) { return 0; }
}
