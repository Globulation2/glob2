// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "TicketVerifier.h"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <set>
#include <stdexcept>

namespace Relay
{
namespace
{
	using json = nlohmann::json;

	TicketResult fail(TicketError error, const std::string& detail)
	{
		TicketResult r;
		r.error = error;
		r.detail = detail;
		return r;
	}

	bool isLowerHex(const std::string& s, std::size_t length)
	{
		if (s.size() != length)
			return false;
		for (char c : s)
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
				return false;
		return true;
	}

	// Uuid in the protocol package: lowercase 8-4-4-4-12 hex.
	bool isUuid(const json& v)
	{
		if (!v.is_string())
			return false;
		const auto& s = v.get_ref<const std::string&>();
		if (s.size() != 36)
			return false;
		for (std::size_t i = 0; i < s.size(); ++i)
		{
			const char c = s[i];
			if (i == 8 || i == 13 || i == 18 || i == 23)
			{
				if (c != '-')
					return false;
			}
			else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
				return false;
		}
		return true;
	}

	bool integerIn(const json& v, std::int64_t lo, std::int64_t hi, std::int64_t& out)
	{
		if (!v.is_number_integer())
			return false;
		if (v.is_number_unsigned())
		{
			const auto u = v.get<std::uint64_t>();
			if (u > static_cast<std::uint64_t>(hi))
				return false;
			out = static_cast<std::int64_t>(u);
		}
		else
			out = v.get<std::int64_t>();
		return out >= lo && out <= hi;
	}

	bool decodeJsonSegment(const std::string& segment, json& out)
	{
		std::vector<std::uint8_t> bytes;
		if (!base64UrlDecode(segment, bytes))
			return false;
		out = json::parse(bytes.begin(), bytes.end(), nullptr, false);
		return !out.is_discarded();
	}

	bool validUrl(const std::string& s)
	{
		if (s.empty() || s.size() > 2048)
			return false;
		static const char* schemes[] = {"https://", "wss://", "http://", "ws://"};
		for (const char* scheme : schemes)
		{
			const std::string prefix(scheme);
			if (s.compare(0, prefix.size(), prefix) == 0 && s.size() > prefix.size())
			{
				for (char c : s)
					if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v')
						return false;
				return true;
			}
		}
		return false;
	}

	std::string checkClaims(const json& c, TicketClaims& out)
	{
		if (!c.contains("iss") || !c["iss"].is_string())
			return "iss must be a string";
		out.issuer = c["iss"].get<std::string>();
		if (!c.contains("sub") || !isUuid(c["sub"]))
			return "sub must be a UUID";
		out.subject = c["sub"].get<std::string>();
		if (!c.contains("jti") || !isUuid(c["jti"]))
			return "jti must be a UUID";
		out.ticketId = c["jti"].get<std::string>();
		std::int64_t n = 0;
		if (!c.contains("iat") || !integerIn(c["iat"], 0, INT64_MAX, n))
			return "iat must be a non-negative integer";
		out.issuedAt = n;
		if (!c.contains("exp") || !integerIn(c["exp"], 0, INT64_MAX, n))
			return "exp must be a non-negative integer";
		out.expiresAt = n;
		if (c.contains("nbf"))
		{
			if (!integerIn(c["nbf"], 0, INT64_MAX, n))
				return "nbf must be a non-negative integer";
			out.notBefore = n;
		}
		if (!c.contains("matchId") || !isUuid(c["matchId"]))
			return "matchId must be a UUID";
		out.matchId = c["matchId"].get<std::string>();
		if (!c.contains("accountId") || !isUuid(c["accountId"]))
			return "accountId must be a UUID";
		out.accountId = c["accountId"].get<std::string>();
		if (out.accountId != out.subject)
			return "sub must equal accountId";
		if (!c.contains("seat") || !integerIn(c["seat"], 0, MAX_TICKET_SEATS - 1, n))
			return "seat must be a seat index";
		out.seat = static_cast<int>(n);

		if (!c.contains("simVersion") || !c["simVersion"].is_object())
			return "simVersion must be an object";
		const json& v = c["simVersion"];
		for (auto it = v.begin(); it != v.end(); ++it)
			if (it.key() != "versionMinor" && it.key() != "netProtocol" && it.key() != "dataHash")
				return "simVersion has an unknown property";
		if (!v.contains("versionMinor") || !integerIn(v["versionMinor"], 0, 65535, n))
			return "simVersion.versionMinor must be 0..65535";
		out.simVersion.versionMinor = static_cast<int>(n);
		if (!v.contains("netProtocol") || !integerIn(v["netProtocol"], 0, 65535, n))
			return "simVersion.netProtocol must be 0..65535";
		out.simVersion.netProtocol = static_cast<int>(n);
		if (!v.contains("dataHash") || !v["dataHash"].is_string() ||
		    !isLowerHex(v["dataHash"].get<std::string>(), 64))
			return "simVersion.dataHash must be lowercase SHA-256 hex";
		out.simVersion.dataHash = v["dataHash"].get<std::string>();

		if (!c.contains("humanSeats") || !c["humanSeats"].is_array())
			return "humanSeats must be an array";
		const json& seats = c["humanSeats"];
		if (seats.empty() || seats.size() > static_cast<std::size_t>(MAX_TICKET_SEATS))
			return "humanSeats must list 1..12 seats";
		out.humanSeatMask = 0;
		for (const auto& s : seats)
		{
			if (!integerIn(s, 0, MAX_TICKET_SEATS - 1, n))
				return "humanSeats must hold seat indices";
			const std::uint32_t bit = 1u << n;
			if (out.humanSeatMask & bit)
				return "humanSeats must be unique";
			out.humanSeatMask |= bit;
		}
		if (!(out.humanSeatMask & (1u << out.seat)))
			return "seat is not a human seat";
		if (!c.contains("relayUrl") || !c["relayUrl"].is_string() || !validUrl(c["relayUrl"].get<std::string>()))
			return "relayUrl must be an http(s) or ws(s) URL";
		out.relayUrl = c["relayUrl"].get<std::string>();
		// entitlements are ignored by relays today (plan section M).
		return {};
	}

	int base64Value(char c)
	{
		if (c >= 'A' && c <= 'Z')
			return c - 'A';
		if (c >= 'a' && c <= 'z')
			return c - 'a' + 26;
		if (c >= '0' && c <= '9')
			return c - '0' + 52;
		if (c == '-')
			return 62;
		if (c == '_')
			return 63;
		return -1;
	}
}

const char* ticketErrorName(TicketError error)
{
	switch (error)
	{
	case TicketError::None: return "none";
	case TicketError::Malformed: return "malformed";
	case TicketError::Algorithm: return "algorithm";
	case TicketError::Type: return "type";
	case TicketError::Key: return "key";
	case TicketError::Signature: return "signature";
	case TicketError::Expired: return "expired";
	case TicketError::Audience: return "audience";
	case TicketError::NotYetValid: return "not_yet_valid";
	case TicketError::Claims: return "claims";
	}
	return "unknown";
}

std::string SimVersion::key() const
{
	return std::to_string(versionMinor) + "-" + std::to_string(netProtocol) + "-" + dataHash;
}

bool base64UrlDecode(const std::string& in, std::vector<std::uint8_t>& out)
{
	out.clear();
	if (in.size() % 4 == 1)
		return false;
	out.reserve(in.size() * 3 / 4);
	std::uint32_t buffer = 0;
	int bits = 0;
	for (char c : in)
	{
		const int v = base64Value(c);
		if (v < 0)
			return false;
		buffer = (buffer << 6) | static_cast<std::uint32_t>(v);
		bits += 6;
		if (bits >= 8)
		{
			bits -= 8;
			out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xff));
		}
	}
	// Leftover bits must be zero, so every byte string has exactly one encoding.
	return (buffer & ((1u << bits) - 1)) == 0;
}

std::string base64UrlEncode(const std::uint8_t* data, std::size_t size)
{
	static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	std::string out;
	out.reserve((size * 4 + 2) / 3);
	std::uint32_t buffer = 0;
	int bits = 0;
	for (std::size_t i = 0; i < size; ++i)
	{
		buffer = (buffer << 8) | data[i];
		bits += 8;
		while (bits >= 6)
		{
			bits -= 6;
			out.push_back(alphabet[(buffer >> bits) & 63]);
		}
	}
	if (bits > 0)
		out.push_back(alphabet[(buffer << (6 - bits)) & 63]);
	return out;
}

struct KeySet::Key
{
	EVP_PKEY* pkey = nullptr;
	~Key() { EVP_PKEY_free(pkey); }
};

KeySet::KeySet() = default;
KeySet::~KeySet() = default;

std::shared_ptr<const KeySet> KeySet::parse(const std::string& text)
{
	const json doc = json::parse(text, nullptr, false);
	if (doc.is_discarded() || !doc.is_object() || !doc.contains("keys") || !doc["keys"].is_array())
		throw std::runtime_error("JWKS must be an object with a keys array");
	auto set = std::make_shared<KeySet>();
	for (const auto& k : doc["keys"])
	{
		if (!k.is_object())
			throw std::runtime_error("JWKS key must be an object");
		if (k.value("kty", "") != "OKP" || k.value("crv", "") != "Ed25519")
			continue;
		if (k.contains("use") && k["use"] != "sig")
			continue;
		if (k.contains("alg") && k["alg"] != "EdDSA")
			continue;
		if (!k.contains("kid") || !k["kid"].is_string() || k["kid"].get<std::string>().empty() ||
		    !k.contains("x") || !k["x"].is_string())
			throw std::runtime_error("Ed25519 JWK needs kid and x");
		std::vector<std::uint8_t> raw;
		if (!base64UrlDecode(k["x"].get<std::string>(), raw) || raw.size() != 32)
			throw std::runtime_error("Ed25519 JWK x must be 32 bytes of base64url");
		auto key = std::make_unique<Key>();
		key->pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, raw.data(), raw.size());
		if (!key->pkey)
			throw std::runtime_error("OpenSSL rejected an Ed25519 public key");
		set->keys[k["kid"].get<std::string>()] = std::move(key);
	}
	return set;
}

std::vector<std::string> KeySet::kids() const
{
	std::vector<std::string> out;
	for (const auto& k : keys)
		out.push_back(k.first);
	return out;
}

bool KeySet::verify(const std::string& kid, const std::string& message, const std::vector<std::uint8_t>& signature) const
{
	auto it = keys.find(kid);
	if (it == keys.end() || signature.size() != 64)
		return false;
	EVP_MD_CTX* ctx = EVP_MD_CTX_new();
	if (!ctx)
		return false;
	bool ok = EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, it->second->pkey) == 1 &&
	          EVP_DigestVerify(ctx, signature.data(), signature.size(),
	                           reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1;
	EVP_MD_CTX_free(ctx);
	return ok;
}

std::string ticketKeyId(const std::string& token)
{
	const auto dot = token.find('.');
	if (dot == std::string::npos)
		return {};
	json header;
	if (!decodeJsonSegment(token.substr(0, dot), header) || !header.is_object() || !header.contains("kid") ||
	    !header["kid"].is_string())
		return {};
	return header["kid"].get<std::string>();
}

TicketResult verifyTicket(const std::string& token, const KeySet& keys, const VerifyOptions& options)
{
	// Trailing whitespace from a file is not part of a token.
	std::string t = token;
	while (!t.empty() && (t.back() == '\n' || t.back() == '\r' || t.back() == ' '))
		t.pop_back();
	const auto first = t.find('.');
	const auto second = first == std::string::npos ? std::string::npos : t.find('.', first + 1);
	if (first == std::string::npos || second == std::string::npos || t.find('.', second + 1) != std::string::npos)
		return fail(TicketError::Malformed, "token must have three segments");
	const std::string headerPart = t.substr(0, first);
	const std::string claimsPart = t.substr(first + 1, second - first - 1);
	const std::string signaturePart = t.substr(second + 1);

	json header;
	if (!decodeJsonSegment(headerPart, header))
		return fail(TicketError::Malformed, "segment is not base64url JSON");
	if (!header.is_object())
		return fail(TicketError::Malformed, "header must be an object");
	if (!header.contains("alg") || header["alg"] != "EdDSA")
		return fail(TicketError::Algorithm, "alg must be EdDSA");
	if (!header.contains("typ") || header["typ"] != MATCH_TICKET_TYPE)
		return fail(TicketError::Type, std::string("typ must be ") + MATCH_TICKET_TYPE);
	if (!header.contains("kid") || !header["kid"].is_string())
		return fail(TicketError::Key, "kid missing");
	const std::string kid = header["kid"].get<std::string>();
	if (!keys.has(kid))
	{
		auto r = fail(TicketError::Key, "unknown kid " + kid.substr(0, 128));
		r.kid = kid;
		return r;
	}
	std::vector<std::uint8_t> signature;
	if (!base64UrlDecode(signaturePart, signature) || !keys.verify(kid, headerPart + "." + claimsPart, signature))
		return fail(TicketError::Signature, "signature does not verify");

	json claims;
	if (!decodeJsonSegment(claimsPart, claims))
		return fail(TicketError::Malformed, "segment is not base64url JSON");
	if (!claims.is_object())
		return fail(TicketError::Malformed, "claims must be an object");
	if (!claims.contains("aud") || claims["aud"] != MATCH_TICKET_AUDIENCE)
		return fail(TicketError::Audience, "wrong audience");
	const std::int64_t now = options.nowSeconds;
	const std::int64_t leeway = options.leewaySeconds;
	if (!claims.contains("exp") || !claims["exp"].is_number() ||
	    claims["exp"].get<double>() + static_cast<double>(leeway) <= static_cast<double>(now))
		return fail(TicketError::Expired, "token expired");
	if (claims.contains("nbf") && claims["nbf"].is_number() &&
	    claims["nbf"].get<double>() - static_cast<double>(leeway) > static_cast<double>(now))
		return fail(TicketError::NotYetValid, "token not yet valid");

	TicketResult result;
	result.kid = kid;
	const std::string problem = checkClaims(claims, result.claims);
	if (!problem.empty())
		return fail(TicketError::Claims, problem);
	if (!options.issuer.empty() && result.claims.issuer != options.issuer)
		return fail(TicketError::Claims, "unexpected issuer");
	return result;
}

MatchAgreement checkAgreement(const SimVersion& matchVersion, std::uint32_t matchHumanSeats, const TicketClaims& claims)
{
	if (!(claims.simVersion == matchVersion))
		return MatchAgreement::SimVersionDiffers;
	if (claims.humanSeatMask != matchHumanSeats)
		return MatchAgreement::HumanSeatsDiffer;
	return MatchAgreement::Agrees;
}
}
