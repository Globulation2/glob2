// SPDX-License-Identifier: GPL-3.0-or-later
#include "Sha256.h"

#include <algorithm>
#include <cstring>

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
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline std::uint32_t rotr(std::uint32_t x, int n)
{
	return (x >> n) | (x << (32 - n));
}
} // namespace

Sha256::Sha256()
	: state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
			0x5be0cd19}
{
}

void Sha256::block(const std::uint8_t *data)
{
	std::uint32_t w[64];
	for (int i = 0; i < 16; ++i)
		w[i] = std::uint32_t(data[i * 4]) << 24 | std::uint32_t(data[i * 4 + 1]) << 16 |
			   std::uint32_t(data[i * 4 + 2]) << 8 | std::uint32_t(data[i * 4 + 3]);
	for (int i = 16; i < 64; ++i)
	{
		const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
		const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	auto a = state[0], b = state[1], c = state[2], d = state[3];
	auto e = state[4], f = state[5], g = state[6], h = state[7];
	for (int i = 0; i < 64; ++i)
	{
		const auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
		const auto choose = (e & f) ^ (~e & g);
		const auto t1 = h + s1 + choose + K[i] + w[i];
		const auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
		const auto majority = (a & b) ^ (a & c) ^ (b & c);
		const auto t2 = s0 + majority;
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
	state[5] += f;
	state[6] += g;
	state[7] += h;
}

void Sha256::update(const void *data, std::size_t size)
{
	auto bytes = static_cast<const std::uint8_t *>(data);
	length += size;
	if (buffered)
	{
		const auto take = std::min(size, buffer.size() - buffered);
		std::memcpy(buffer.data() + buffered, bytes, take);
		buffered += take;
		bytes += take;
		size -= take;
		if (buffered < buffer.size())
			return;
		block(buffer.data());
		buffered = 0;
	}
	for (; size >= 64; size -= 64, bytes += 64)
		block(bytes);
	std::memcpy(buffer.data(), bytes, size);
	buffered = size;
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
		tail[i] = std::uint8_t(bits >> (56 - 8 * i));
	update(tail, 8);
	Digest out;
	for (int i = 0; i < 8; ++i)
		for (int j = 0; j < 4; ++j)
			out[i * 4 + j] = std::uint8_t(state[i] >> (24 - 8 * j));
	return out;
}

Sha256::Digest Sha256::digest(std::string_view data)
{
	Sha256 hash;
	hash.update(data);
	return hash.finish();
}

std::string Sha256::toHex(const Digest &digest)
{
	static const char digits[] = "0123456789abcdef";
	std::string text;
	text.reserve(64);
	for (auto byte : digest)
	{
		text.push_back(digits[byte >> 4]);
		text.push_back(digits[byte & 15]);
	}
	return text;
}

std::string Sha256::hex(std::string_view data)
{
	return toHex(digest(data));
}

bool Sha256::isHexDigest(std::string_view text)
{
	if (text.size() != 64)
		return false;
	for (char c : text)
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return false;
	return true;
}
