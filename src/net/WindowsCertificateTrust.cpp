// SPDX-License-Identifier: GPL-3.0-or-later
#include "mobile/CertificateTrust.h"
#include <windows.h>
#include <wincrypt.h>
#include <memory>

bool MobileCertificateTrust::platformVerify(const Chain &encoded, const std::string &hostname)
{
	if (encoded.empty() || encoded.size() > 16)
		return false;
	const auto store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
	if (!store)
		return false;
	struct Store
	{
		HCERTSTORE value;
		~Store()
		{
			CertCloseStore(value, 0);
		}
	} owned{store};
	PCCERT_CONTEXT leaf = nullptr;
	struct Certificate
	{
		PCCERT_CONTEXT &value;
		~Certificate()
		{
			if (value)
				CertFreeCertificateContext(value);
		}
	} certificate{leaf};
	for (size_t i = 0; i < encoded.size(); ++i)
	{
		PCCERT_CONTEXT added = nullptr;
		if (!CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, encoded[i].data(),
											  static_cast<DWORD>(encoded[i].size()),
											  CERT_STORE_ADD_ALWAYS, &added))
			return false;
		if (i == 0)
			leaf = added;
		else
			CertFreeCertificateContext(added);
	}
	CERT_CHAIN_PARA parameters{};
	parameters.cbSize = sizeof(parameters);
	LPSTR usage = const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH);
	parameters.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
	parameters.RequestedUsage.Usage.cUsageIdentifier = 1;
	parameters.RequestedUsage.Usage.rgpszUsageIdentifier = &usage;
	PCCERT_CHAIN_CONTEXT chain = nullptr;
	// Only cached intermediates/roots: never block the game loop on PKI downloads.
	if (!CertGetCertificateChain(nullptr, leaf, nullptr, store, &parameters,
								 CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL | CERT_CHAIN_DISABLE_AIA |
									 CERT_CHAIN_DISABLE_AUTH_ROOT_AUTO_UPDATE,
								 nullptr, &chain))
		return false;
	struct Context
	{
		PCCERT_CHAIN_CONTEXT value;
		~Context()
		{
			CertFreeCertificateChain(value);
		}
	} context{chain};
	std::wstring host(hostname.begin(), hostname.end()); // Callback permits ASCII hostnames only.
	SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl{};
	ssl.cbSize = sizeof(ssl);
	ssl.dwAuthType = AUTHTYPE_SERVER;
	ssl.pwszServerName = host.data();
	CERT_CHAIN_POLICY_PARA policy{};
	policy.cbSize = sizeof(policy);
	policy.pvExtraPolicyPara = &ssl;
	CERT_CHAIN_POLICY_STATUS status{};
	status.cbSize = sizeof(status);
	return CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status) &&
		   status.dwError == 0;
}
