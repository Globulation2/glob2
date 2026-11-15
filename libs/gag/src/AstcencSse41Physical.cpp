// SPDX-License-Identifier: GPL-3.0-or-later
// The rest of astcenc's SSE4.1 copy (see AstcencSse41.cpp): its block packing,
// whose file-local bit helpers share names with astcenc_integer_sequence.cpp's.
#if defined(GLOB2_SSE41_COPY) && defined(__SSE4_1__) && (defined(__x86_64__) || defined(__i386__)) && \
	(defined(__GNUC__) || defined(__clang__))
#include "AstcencSse41Prelude.inl"

namespace astcenc_sse41
{
#include "../../../third_party/astcenc/astcenc/astcenc_symbolic_physical.cpp"
} // namespace astcenc_sse41
#endif
