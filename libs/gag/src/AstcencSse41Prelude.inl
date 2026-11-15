// SPDX-License-Identifier: GPL-3.0-or-later
// What both halves of astcenc's SSE4.1 copy (AstcencSse41.cpp and
// AstcencSse41Physical.cpp) set up before including its sources inside
// namespace astcenc_sse41. Two translation units because two of astcenc's
// sources define file-local helpers with the same names.

// Every system header the sources include, first, so their include guards
// keep them out of the namespace.
#include <algorithm>
#include <array>
#include <assert.h>
#include <atomic>
#include <cassert>
#include <cfenv>
#include <cfloat>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <immintrin.h>
#include <limits>
#include <mutex>
#include <new>
#include <stdio.h>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// SSE4.1 alone: the processors that run it but not POPCNT (Penryn) still
// qualify, and astcenc counts bits in software without it.
#define ASTCENC_POPCNT 0
#define ASTCENC_AVX 0
// The C API keeps C linkage inside the namespace; distinct names keep these
// definitions apart from the portable build's.
#define astcenc_config_init astcenc_sse41_config_init
#define astcenc_context_alloc astcenc_sse41_context_alloc
#define astcenc_compress_image astcenc_sse41_compress_image
#define astcenc_compress_reset astcenc_sse41_compress_reset
#define astcenc_compress_cancel astcenc_sse41_compress_cancel
#define astcenc_decompress_image astcenc_sse41_decompress_image
#define astcenc_decompress_reset astcenc_sse41_decompress_reset
#define astcenc_context_free astcenc_sse41_context_free
#define astcenc_get_block_info astcenc_sse41_get_block_info
#define astcenc_get_error_string astcenc_sse41_get_error_string
