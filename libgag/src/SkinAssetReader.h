// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Bounded little-endian reading and the checks the GSR1 and GSB1 decoders
// share. Validation arithmetic is binary64 in both the native and the Studio
// decoders; limits on serialized scalars use their binary32 representation,
// including endpoints, so every decoder accepts exactly the same bytes.
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace GAGCore::SkinAsset
{
constexpr std::size_t MaximumBytes = 16 * 1024 * 1024;
constexpr unsigned MaximumVertices = 8192, MaximumIndices = 49152, MaximumClips = 8,
				   MaximumLogicalSize = 128;
constexpr float MaximumScalar = 10000;
constexpr double Tolerance = 0.0001;
constexpr double MaximumHeading = double(6.283186f);

inline void require(bool ok, const char *error)
{
	if (!ok)
		throw std::runtime_error(error);
}

struct Reader
{
	std::span<const std::uint8_t> bytes;
	std::size_t at = 0;

	std::uint32_t word()
	{
		require(bytes.size() - at >= 4, "truncated skin asset");
		const auto *p = bytes.data() + at;
		at += 4;
		return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 |
			   std::uint32_t(p[3]) << 24;
	}
	std::int16_t half()
	{
		require(bytes.size() - at >= 2, "truncated skin asset");
		const std::uint16_t v = std::uint16_t(bytes[at]) | std::uint16_t(bytes[at + 1]) << 8;
		at += 2;
		return std::bit_cast<std::int16_t>(v);
	}
	float scalar(float bound = MaximumScalar)
	{
		const float v = std::bit_cast<float>(word());
		require(std::isfinite(v) && std::abs(v) <= bound, "invalid skin asset scalar");
		return v;
	}
	template <std::size_t N> void read(std::array<float, N> &values)
	{
		for (auto &v : values)
			v = scalar();
	}
	bool finished() const { return at == bytes.size(); }
};

// Every clip carries its orthographic camera: an affine, invertible model-to-
// clip matrix and an orthonormal model-to-camera matrix for normals.
inline void requireCamera(const std::array<float, 16> &m, const std::array<float, 9> &n)
{
	require(m[12] == 0 && m[13] == 0 && m[14] == 0 && m[15] == 1, "invalid clip camera");
	const double determinant = double(m[0]) * (double(m[5]) * m[10] - double(m[6]) * m[9]) -
							   double(m[1]) * (double(m[4]) * m[10] - double(m[6]) * m[8]) +
							   double(m[2]) * (double(m[4]) * m[9] - double(m[5]) * m[8]);
	require(std::abs(determinant) > 1e-12, "singular clip camera");
	for (unsigned i = 0; i < 3; ++i)
		for (unsigned j = 0; j < 3; ++j)
		{
			double dot = 0;
			for (unsigned k = 0; k < 3; ++k)
				dot += double(n[i * 3 + k]) * n[j * 3 + k];
			require(std::abs(dot - (i == j ? 1. : 0.)) < Tolerance, "invalid clip normal camera");
		}
}

// Unit length, or +Z when opposing influences cancel the normal. The GLSL
// deformation shader and the Studio evaluators apply the same rule.
inline void normalize(float *n)
{
	const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	if (length < 1e-8f)
	{
		n[0] = 0;
		n[1] = 0;
		n[2] = 1;
	}
	else
		for (unsigned k = 0; k < 3; ++k)
			n[k] /= length;
}
} // namespace GAGCore::SkinAsset
