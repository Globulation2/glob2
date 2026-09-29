// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <openssl/x509_vfy.h>
#include <string>
#include <vector>
namespace MobileCertificateTrust {
using Chain = std::vector<std::vector<unsigned char>>;
// OpenSSL callback: platform chain policy AND explicit DNS/IP identity must pass.
int verify(X509_STORE_CTX* context, void* hostname) noexcept;
bool platformVerify(const Chain& chain, const std::string& hostname);
}
