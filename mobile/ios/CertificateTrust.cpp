// SPDX-License-Identifier: GPL-3.0-or-later
#include "../CertificateTrust.h"
#include <Security/Security.h>

namespace {
template<class T> struct Owned {
    T value = nullptr;
    ~Owned() { if (value) CFRelease(value); }
};
}
bool MobileCertificateTrust::platformVerify(const Chain& chain, const std::string& hostname) {
    if (chain.empty() || chain.size() > 16 || hostname.empty()) return false;
    Owned<CFMutableArrayRef> certificates{CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks)};
    if (!certificates.value) return false;
    size_t total = 0;
    for (const auto& encoded : chain) {
        if (encoded.empty() || encoded.size() > 65536 || encoded.size() > 131072 - total) return false;
        total += encoded.size();
        Owned<CFDataRef> data{CFDataCreate(kCFAllocatorDefault, encoded.data(), encoded.size())};
        if (!data.value) return false;
        Owned<SecCertificateRef> certificate{SecCertificateCreateWithData(kCFAllocatorDefault, data.value)};
        if (!certificate.value) return false;
        CFArrayAppendValue(certificates.value, certificate.value);
    }
    Owned<CFStringRef> host{CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(hostname.data()), hostname.size(), kCFStringEncodingUTF8, false)};
    if (!host.value) return false;
    Owned<SecPolicyRef> policy{SecPolicyCreateSSL(true, host.value)};
    Owned<SecTrustRef> trust;
    if (!policy.value || SecTrustCreateWithCertificates(certificates.value, policy.value, &trust.value) != errSecSuccess) return false;
    // Require a complete served chain; certificate discovery must not block frames.
    if (SecTrustSetNetworkFetchAllowed(trust.value, false) != errSecSuccess) return false;
    return SecTrustEvaluateWithError(trust.value, nullptr);
}
