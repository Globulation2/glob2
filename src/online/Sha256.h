// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// A small portable SHA-256 (FIPS 180-4). The online code needs it on every target,
// including the browser build, which has no OpenSSL: map hashes, the simulation data
// hash and the verifier's artifact hashes.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Online
{
	class Sha256
	{
	public:
		using Digest = std::array<std::uint8_t, 32>;

		Sha256();
		void update(const void* data, std::size_t size);
		void update(const std::string& bytes) { update(bytes.data(), bytes.size()); }
		/// Finishes the hash; the object must not be updated afterwards.
		Digest finish();

		static Digest of(const void* data, std::size_t size);
		static Digest of(const std::string& bytes) { return of(bytes.data(), bytes.size()); }
		static Digest of(const std::vector<std::uint8_t>& bytes) { return of(bytes.data(), bytes.size()); }

	private:
		void block(const std::uint8_t* data);

		std::array<std::uint32_t, 8> state;
		std::array<std::uint8_t, 64> buffer{};
		std::size_t buffered = 0;
		std::uint64_t length = 0;
	};

	/// Lowercase hexadecimal.
	std::string toHex(const std::uint8_t* data, std::size_t size);
	inline std::string toHex(const Sha256::Digest& digest) { return toHex(digest.data(), digest.size()); }
	/// Parses exactly 64 lowercase hex digits; false otherwise.
	bool parseSha256Hex(const std::string& text, Sha256::Digest& out);
}
