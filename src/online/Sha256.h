// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Portable SHA-256 (FIPS 180-4) for content addressing. The platform names map
// versions by the SHA-256 of their decompressed bytes; the browser build has no
// OpenSSL, so the client carries its own implementation.
class Sha256
{
  public:
	using Digest = std::array<std::uint8_t, 32>;
	Sha256();
	void update(const void *data, std::size_t size);
	void update(std::string_view data)
	{
		update(data.data(), data.size());
	}
	Digest finish();

	static Digest digest(std::string_view data);
	// 64 lowercase hex digits.
	static std::string hex(std::string_view data);
	static std::string toHex(const Digest &digest);
	// True for exactly 64 lowercase hex digits, the platform's sha256_hex form.
	static bool isHexDigest(std::string_view text);

  private:
	void block(const std::uint8_t *data);
	std::array<std::uint32_t, 8> state;
	std::array<std::uint8_t, 64> buffer{};
	std::size_t buffered = 0;
	std::uint64_t length = 0;
};
