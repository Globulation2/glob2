// SPDX-License-Identifier: GPL-3.0-or-later
// A torus 3x3 smoothing stencil in the shape of the map generators' kernels
// (src/map/generator/shared/{HeightMap,Patterns,Terrain}.cpp). It prints a checksum
// of the smoothed field; tools/compiler/check_stencil_vectorizer.py compiles it at
// several optimization levels (with AddressSanitizer where available) and requires
// identical output. It guards against a suspected Apple clang 21 -O2 vectorizer
// miscompilation of exactly this loop shape.
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{
std::uint32_t next(std::uint32_t &state)
{
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

void smooth(const std::vector<int> &in, std::vector<int> &out, int w, int h)
{
	for (int y = 0; y < h; ++y)
	{
		const int up = (y + h - 1) % h, down = (y + 1) % h;
		for (int x = 0; x < w; ++x)
		{
			const int left = (x + w - 1) % w, right = (x + 1) % w;
			int sum = 0;
			for (const int row : {up, y, down})
				sum += in[row * w + left] + in[row * w + x] + in[row * w + right];
			out[y * w + x] = sum / 9;
		}
	}
}
}

int main()
{
	std::uint64_t checksum = 1469598103934665603ull;
	for (const auto [w, h] : {std::pair{64, 64}, {127, 61}, {256, 128}, {33, 257}})
	{
		std::vector<int> a(std::size_t(w) * h), b(a.size());
		std::uint32_t state = 2463534242u ^ unsigned(w * 7919 + h);
		for (int &value : a)
			value = int(next(state) % 65536);
		for (int pass = 0; pass < 8; ++pass)
		{
			smooth(a, b, w, h);
			a.swap(b);
		}
		for (const int value : a)
			checksum = (checksum ^ std::uint64_t(value)) * 1099511628211ull;
	}
	std::printf("%016llx\n", static_cast<unsigned long long>(checksum));
	return 0;
}
