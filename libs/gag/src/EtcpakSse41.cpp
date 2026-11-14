// SPDX-License-Identifier: GPL-3.0-or-later
// Compiles etcpak's encoders a second time, with SSE4.1 enabled, inside a
// namespace so their symbols stay apart from the portable build. The build
// adds -msse4.1 to this file alone on x86 (CMakeLists.txt); without it the
// file defines no encoders and available() reports none.
#include "EtcpakSse41.h"

#if defined(__SSE4_1__) && (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#define GLOB2_ETCPAK_SSE41 1
// Every system header the encoders include, first, so their include guards
// keep them out of the namespace below.
#include <algorithm>
#include <array>
#include <assert.h>
#include <cmath>
#include <limits>
#include <math.h>
#include <smmintrin.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <x86intrin.h>

namespace etcpak_sse41
{
#include "../../../third_party/etcpak/etcpak/Tables.cpp"
#include "../../../third_party/etcpak/etcpak/Dither.cpp"
#include "../../../third_party/etcpak/etcpak/ProcessRGB.cpp"
// Its file-local helpers share names with ProcessRGB's; the shared tables and
// dithering above stay visible from the enclosing namespace.
namespace dxtc
{
#include "../../../third_party/etcpak/etcpak/ProcessDxtc.cpp"
} // namespace dxtc
} // namespace etcpak_sse41
#endif

namespace GAGCore::EtcpakSse41
{
Encoders available()
{
#ifdef GLOB2_ETCPAK_SSE41
	static const bool supported = __builtin_cpu_supports("sse4.1");
	if (supported)
		return {&etcpak_sse41::dxtc::CompressBc3,
				[](const std::uint32_t *source, std::uint64_t *blocks, std::uint32_t count, std::size_t width)
				{ etcpak_sse41::CompressEtc2Rgba(source, blocks, count, width, true); }};
#endif
	return {};
}
} // namespace GAGCore::EtcpakSse41
