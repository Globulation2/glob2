// SPDX-License-Identifier: GPL-3.0-or-later
#include "AstcEncoder.h"
#include <astcenc/astcenc.h>
#include <cstddef>

// astcenc picks its SIMD back end from the compiler's target macros: NEON on
// arm64, SSE4.2 on Android x86_64, SSE2 on desktop x86-64, scalar elsewhere.
namespace GAGCore::AstcEncoder
{
namespace
{
// The read-only tables for 4x4 blocks (most of a context's memory), built once
// and kept for the process: worker threads may still hold child contexts that
// read them while the process exits.
const astcenc_context *rootContext()
{
	static const astcenc_context *const root = []() -> astcenc_context *
	{
		astcenc_config config;
		// Phones encode every HD frame as it streams in, so the encoder's
		// search is trimmed below even the fastest preset: one partition, one
		// candidate, one refinement pass. On the shipped HD frames that is
		// about 1.6 times faster than the preset for about 1.3 dB of PSNR (at
		// about 50 dB); see docs/architecture/rendering.md.
		astcenc_context *context = nullptr;
		if (astcenc_config_init(ASTCENC_PRF_LDR, 4, 4, 1, ASTCENC_PRE_FASTEST, 0, &config) != ASTCENC_SUCCESS)
			return nullptr;
		config.tune_partition_count_limit = 1;
		config.tune_candidate_limit = 1;
		config.tune_refinement_limit = 1;
		if (astcenc_context_alloc(&config, 1, &context, nullptr) != ASTCENC_SUCCESS)
			return nullptr;
		return context;
	}();
	return root;
}
// A child context for each worker thread, sharing the root's tables: a
// context compresses one image at a time, and workers are long-lived.
struct ThreadContext
{
	astcenc_context *context = nullptr;
	bool failed = false;
	ThreadContext() = default;
	ThreadContext(const ThreadContext &) = delete;
	ThreadContext &operator=(const ThreadContext &) = delete;
	~ThreadContext()
	{
		if (context) astcenc_context_free(context);
	}
	astcenc_context *get()
	{
		if (context || failed) return context;
		const astcenc_context *root = rootContext();
		failed = !root || astcenc_context_alloc(nullptr, 1, &context, root) != ASTCENC_SUCCESS;
		if (failed) context = nullptr;
		return context;
	}
};
astcenc_context *threadContext()
{
	static thread_local ThreadContext value;
	return value.get();
}
// rgba: tightly packed RGBA8, red first; width and height multiples of 4.
// blocks: (width / 4) * (height / 4) * 16 bytes.
bool encodeBlocks(const unsigned char *rgba, int width, int height, unsigned char *blocks)
{
	astcenc_context *context = threadContext();
	if (!context) return false;
	void *slice = const_cast<unsigned char *>(rgba);
	astcenc_image image{unsigned(width), unsigned(height), 1, ASTCENC_TYPE_U8, &slice};
	const astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
	const auto bytes = static_cast<size_t>(width / 4) * static_cast<size_t>(height / 4) * 16;
	const bool encoded = astcenc_compress_image(context, &image, &swizzle, blocks, bytes, 0) == ASTCENC_SUCCESS;
	astcenc_compress_reset(context);
	return encoded;
}
// The inverse, for tests: RGBA8 texels as an ASTC LDR decoder produces them.
bool decodeBlocks(const unsigned char *blocks, int width, int height, unsigned char *rgba)
{
	astcenc_context *context = threadContext();
	if (!context) return false;
	void *slice = rgba;
	astcenc_image image{unsigned(width), unsigned(height), 1, ASTCENC_TYPE_U8, &slice};
	const astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
	const auto bytes = static_cast<size_t>((width + 3) / 4) * static_cast<size_t>((height + 3) / 4) * 16;
	const bool decoded = astcenc_decompress_image(context, blocks, bytes, &image, &swizzle, 0) == ASTCENC_SUCCESS;
	astcenc_decompress_reset(context);
	return decoded;
}
} // namespace
std::vector<unsigned char> encode(const unsigned char *rgba, int width, int height)
{
	std::vector<unsigned char> blocks;
	if (width < 4 || height < 4 || width % 4 || height % 4) return blocks;
	blocks.resize(std::size_t(width / 4) * std::size_t(height / 4) * 16);
	if (!encodeBlocks(rgba, width, height, blocks.data())) blocks.clear();
	return blocks;
}
std::vector<unsigned char> decode(const unsigned char *blocks, int width, int height)
{
	std::vector<unsigned char> rgba;
	if (width <= 0 || height <= 0) return rgba;
	rgba.resize(std::size_t(width) * std::size_t(height) * 4);
	if (!decodeBlocks(blocks, width, height, rgba.data())) rgba.clear();
	return rgba;
}
} // namespace GAGCore::AstcEncoder
