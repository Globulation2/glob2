// SPDX-License-Identifier: GPL-3.0-or-later
#include "AstcEncoder.h"
#include "AstcencSse41.h"
#include <astcenc/astcenc.h>
#include <cstddef>

#include "AstcencCodec.inl"

namespace GAGCore::AstcEncoder
{
namespace
{
AstcencSse41::Codecs codecs()
{
	// The SSE4.1 copy where the CPU runs it, otherwise the portable build
	// (SSE2 on x86-64, NEON on arm64, scalar elsewhere).
	static const AstcencSse41::Codecs chosen = []
	{
		const auto simd = AstcencSse41::available();
		return simd.encode ? simd : AstcencSse41::Codecs{&astcenc_glob2::encode, &astcenc_glob2::decode};
	}();
	return chosen;
}
} // namespace
std::vector<unsigned char> encode(const unsigned char *rgba, int width, int height)
{
	std::vector<unsigned char> blocks;
	if (width < 4 || height < 4 || width % 4 || height % 4) return blocks;
	blocks.resize(std::size_t(width / 4) * std::size_t(height / 4) * 16);
	if (!codecs().encode(rgba, width, height, blocks.data())) blocks.clear();
	return blocks;
}
std::vector<unsigned char> decode(const unsigned char *blocks, int width, int height)
{
	std::vector<unsigned char> rgba;
	if (width <= 0 || height <= 0) return rgba;
	rgba.resize(std::size_t(width) * std::size_t(height) * 4);
	if (!codecs().decode(blocks, width, height, rgba.data())) rgba.clear();
	return rgba;
}
bool usesSse41()
{
	return AstcencSse41::available().encode != nullptr;
}
} // namespace GAGCore::AstcEncoder
