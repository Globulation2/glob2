// SPDX-License-Identifier: GPL-3.0-or-later
// Compiles astcenc a second time with SSE4.1 enabled, inside a namespace so
// its symbols stay apart from the portable build, as EtcpakSse41.cpp does for
// etcpak. The build adds -msse4.1 and GLOB2_SSE41_COPY to this file and
// AstcencSse41Physical.cpp alone on native x86 (CMakeLists.txt); without them
// the files define no codec and available() reports none.
#include "AstcencSse41.h"

#if defined(GLOB2_SSE41_COPY) && defined(__SSE4_1__) && (defined(__x86_64__) || defined(__i386__)) && \
	(defined(__GNUC__) || defined(__clang__))
#define GLOB2_ASTCENC_SSE41 1
#include "AstcencSse41Prelude.inl"

namespace astcenc_sse41
{
// astcenc's vector abs() would otherwise hide the integer one inside the namespace.
using ::abs;
#include "../../../third_party/astcenc/astcenc/astcenc_averages_and_directions.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_block_sizes.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_color_quantize.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_color_unquantize.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_compress_symbolic.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_compute_variance.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_decompress_symbolic.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_diagnostic_trace.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_entry.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_find_best_partitioning.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_ideal_endpoints_and_weights.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_image.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_integer_sequence.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_mathlib.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_mathlib_softfloat.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_partition_tables.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_percentile_tables.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_pick_best_endpoint_format.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_quantization.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_weight_align.cpp"
#include "../../../third_party/astcenc/astcenc/astcenc_weight_quant_xfer_tables.cpp"
// astcenc_symbolic_physical.cpp: AstcencSse41Physical.cpp.
#include "AstcencCodec.inl"
} // namespace astcenc_sse41
#endif

namespace GAGCore::AstcencSse41
{
Codecs available()
{
#ifdef GLOB2_ASTCENC_SSE41
	static const bool supported = __builtin_cpu_supports("sse4.1");
	if (supported) return {&astcenc_sse41::astcenc_glob2::encode, &astcenc_sse41::astcenc_glob2::decode};
#endif
	return {};
}
} // namespace GAGCore::AstcencSse41
