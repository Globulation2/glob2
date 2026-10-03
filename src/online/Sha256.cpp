// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "Sha256.h"

#include <algorithm>
#include <cstring>

namespace Online
{
namespace
{
constexpr std::uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
}

Sha256::Sha256()
	: state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}
{
}

void Sha256::block(const std::uint8_t* p)
{
	std::uint32_t w[64];
	for (int i = 0; i < 16; ++i)
		w[i] = (std::uint32_t(p[4 * i]) << 24) | (std::uint32_t(p[4 * i + 1]) << 16) | (std::uint32_t(p[4 * i + 2]) << 8) |
		       std::uint32_t(p[4 * i + 3]);
	for (int i = 16; i < 64; ++i)
	{
		const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
		const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
	std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
	for (int i = 0; i < 64; ++i)
	{
		const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
		const std::uint32_t ch = (e & f) ^ (~e & g);
		const std::uint32_t t1 = h + s1 + ch + K[i] + w[i];
		const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
		const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
		const std::uint32_t t2 = s0 + maj;
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}
	state[0] += a; state[1] += b; state[2] += c; state[3] += d;
	state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void Sha256::update(const void* data, std::size_t size)
{
	const auto* p = static_cast<const std::uint8_t*>(data);
	length += size;
	while (size > 0)
	{
		const std::size_t take = std::min(size, buffer.size() - buffered);
		std::memcpy(buffer.data() + buffered, p, take);
		buffered += take;
		p += take;
		size -= take;
		if (buffered == buffer.size())
		{
			block(buffer.data());
			buffered = 0;
		}
	}
}

Sha256::Digest Sha256::finish()
{
	const std::uint64_t bits = length * 8;
	const std::uint8_t pad = 0x80;
	update(&pad, 1);
	const std::uint8_t zero = 0;
	while (buffered != 56)
		update(&zero, 1);
	std::uint8_t tail[8];
	for (int i = 0; i < 8; ++i)
		tail[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
	update(tail, 8);
	Digest out;
	for (int i = 0; i < 8; ++i)
	{
		out[4 * i] = static_cast<std::uint8_t>(state[i] >> 24);
		out[4 * i + 1] = static_cast<std::uint8_t>(state[i] >> 16);
		out[4 * i + 2] = static_cast<std::uint8_t>(state[i] >> 8);
		out[4 * i + 3] = static_cast<std::uint8_t>(state[i]);
	}
	return out;
}

Sha256::Digest Sha256::of(const void* data, std::size_t size)
{
	Sha256 hash;
	hash.update(data, size);
	return hash.finish();
}

std::string toHex(const std::uint8_t* data, std::size_t size)
{
	static const char digits[] = "0123456789abcdef";
	std::string out;
	out.reserve(size * 2);
	for (std::size_t i = 0; i < size; ++i)
	{
		out.push_back(digits[data[i] >> 4]);
		out.push_back(digits[data[i] & 15]);
	}
	return out;
}

bool parseSha256Hex(const std::string& text, Sha256::Digest& out)
{
	if (text.size() != 64)
		return false;
	auto value = [](char c) -> int {
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		return -1;
	};
	for (std::size_t i = 0; i < 32; ++i)
	{
		const int hi = value(text[2 * i]), lo = value(text[2 * i + 1]);
		if (hi < 0 || lo < 0)
			return false;
		out[i] = static_cast<std::uint8_t>(hi * 16 + lo);
	}
	return true;
}

std::string Sha256::toHex(const Digest& digest)
{
	return Online::toHex(digest.data(), digest.size());
}

std::string Sha256::hex(std::string_view data)
{
	return toHex(digest(data));
}

bool Sha256::isHexDigest(std::string_view text)
{
	Digest ignored;
	return parseSha256Hex(std::string(text), ignored);
}
}
