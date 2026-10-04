// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// Helpers for the relay tests: fixture files, and the protocol package's deterministic
// TEST-ONLY Ed25519 key (SHA-256 of "glob2 protocol fixture key, TEST ONLY", see
// platform/packages/protocol/scripts/fixtureFiles.ts) to sign tickets of our own.

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "TicketVerifier.h"

namespace RelayTest
{
	inline std::string fixtureDir()
	{
		if (const char* dir = std::getenv("GLOB2_RELAY_FIXTURES"))
			return dir;
#ifdef RELAY_FIXTURE_DIR
		return RELAY_FIXTURE_DIR;
#else
		return "test/fixtures/relay-tickets";
#endif
	}

	inline std::string readFixture(const std::string& name)
	{
		std::ifstream in(fixtureDir() + "/" + name, std::ios::binary);
		if (!in)
			throw std::runtime_error("missing fixture " + name);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}

	/// An Ed25519 private key from a 32-byte seed.
	class SigningKey
	{
	public:
		explicit SigningKey(const std::vector<std::uint8_t>& seed)
		{
			key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size());
			if (!key)
				throw std::runtime_error("bad Ed25519 seed");
		}
		~SigningKey() { EVP_PKEY_free(key); }
		SigningKey(const SigningKey&) = delete;
		SigningKey& operator=(const SigningKey&) = delete;

		/// The protocol package's fixture key.
		static std::vector<std::uint8_t> fixtureSeed()
		{
			const std::string phrase = "glob2 protocol fixture key, TEST ONLY";
			std::vector<std::uint8_t> seed(32);
			unsigned int length = 0;
			EVP_Digest(phrase.data(), phrase.size(), seed.data(), &length, EVP_sha256(), nullptr);
			return seed;
		}

		std::string publicX() const
		{
			std::uint8_t raw[32];
			std::size_t length = sizeof(raw);
			EVP_PKEY_get_raw_public_key(key, raw, &length);
			return Relay::base64UrlEncode(raw, length);
		}

		std::string jwk(const std::string& kid) const
		{
			return nlohmann::json{{"kty", "OKP"}, {"crv", "Ed25519"}, {"x", publicX()},
			                      {"kid", kid}, {"alg", "EdDSA"}, {"use", "sig"}}
			    .dump();
		}

		std::string sign(const nlohmann::json& header, const nlohmann::json& claims) const
		{
			const std::string h = header.dump(), c = claims.dump();
			const std::string input = Relay::base64UrlEncode(reinterpret_cast<const std::uint8_t*>(h.data()), h.size()) +
			                          "." +
			                          Relay::base64UrlEncode(reinterpret_cast<const std::uint8_t*>(c.data()), c.size());
			std::uint8_t signature[64];
			std::size_t length = sizeof(signature);
			EVP_MD_CTX* ctx = EVP_MD_CTX_new();
			EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, key);
			EVP_DigestSign(ctx, signature, &length, reinterpret_cast<const unsigned char*>(input.data()), input.size());
			EVP_MD_CTX_free(ctx);
			return input + "." + Relay::base64UrlEncode(signature, length);
		}

	private:
		EVP_PKEY* key = nullptr;
	};

	inline nlohmann::json ticketHeader(const std::string& kid = "fixture-key-1")
	{
		return {{"alg", "EdDSA"}, {"typ", "glob2-match+jwt"}, {"kid", kid}};
	}

	/// The fixture claims (valid at 1790000060).
	inline nlohmann::json fixtureClaims()
	{
		return nlohmann::json::parse(readFixture("expected-claims.json"));
	}
}
