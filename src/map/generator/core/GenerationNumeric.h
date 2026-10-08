// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationWork.h"
#include "third_party/quickjs-ng/Glob2Math.h"
#include <cmath>
namespace MapGeneration::Numeric
{
// Native calls retain their original overloads; script calls use pinned double math.
#define GENERATION_UNARY(name)                                                                     \
	template <class T> auto name(T x)                                                              \
	{                                                                                              \
		using R = decltype(std::name(x));                                                          \
		if (generationWork)                                                                        \
			return static_cast<R>(glob2_math_##name(double(x)));                                   \
		return std::name(x);                                                                       \
	}
#define GENERATION_BINARY(name)                                                                    \
	template <class T, class U> auto name(T x, U y)                                                \
	{                                                                                              \
		using R = decltype(std::name(x, y));                                                       \
		if (generationWork)                                                                        \
			return static_cast<R>(glob2_math_##name(double(x), double(y)));                        \
		return std::name(x, y);                                                                    \
	}
// clang-format off
GENERATION_UNARY(sin) GENERATION_UNARY(cos) GENERATION_UNARY(tan)
GENERATION_UNARY(asin) GENERATION_UNARY(acos) GENERATION_UNARY(atan)
GENERATION_UNARY(sqrt) GENERATION_UNARY(exp) GENERATION_UNARY(log)
GENERATION_UNARY(log1p) GENERATION_UNARY(expm1) GENERATION_UNARY(log2)
GENERATION_UNARY(floor) GENERATION_UNARY(ceil) GENERATION_UNARY(trunc)
GENERATION_BINARY(atan2) GENERATION_BINARY(hypot) GENERATION_BINARY(pow) GENERATION_BINARY(fmod)
// clang-format on
#undef GENERATION_UNARY
#undef GENERATION_BINARY

template <class T> auto round(T x)
{
	using R = decltype(std::round(x));
	if (!generationWork)
		return std::round(x);
	const double magnitude = glob2_math_fabs(double(x));
	const double integer = glob2_math_floor(magnitude);
	return static_cast<R>(glob2_math_copysign(integer + (magnitude - integer >= 0.5), double(x)));
}
template <class T> long lround(T x)
{
	if (generationWork)
		return static_cast<long>(round(x));
	return std::lround(x);
}
// Legacy height/noise sources used the global C double overloads.
#define GENERATION_GLOBAL_UNARY(name)                                                              \
	template <class T> double global_##name(T x)                                                   \
	{                                                                                              \
		return name(double(x));                                                                    \
	}
// clang-format off
GENERATION_GLOBAL_UNARY(sqrt) GENERATION_GLOBAL_UNARY(sin)
GENERATION_GLOBAL_UNARY(cos) GENERATION_GLOBAL_UNARY(asin)
// clang-format on
#undef GENERATION_GLOBAL_UNARY
template <class T, class U> double global_pow(T x, U y)
{
	return pow(double(x), double(y));
}
} // namespace MapGeneration::Numeric
