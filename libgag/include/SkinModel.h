// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace GAGCore
{
// GSR1 uses model-space, right-handed coordinates and row-major matrices.
// Quaternions are xyzw; transforms apply positive uniform scale, rotation,
// then translation. All clips share rest geometry, topology and influences.
struct SkinTransform
{
	std::array<float, 3> translation{};
	std::array<float, 4> rotation{0, 0, 0, 1};
	float scale = 1;
};
struct SkinBone
{
	std::uint32_t parent;
	SkinTransform rest, inverseBind;
};
struct SkinInfluence
{
	std::array<std::uint32_t, 4> bones;
	std::array<float, 4> weights;
};
struct SkinFrame
{
	float heading, time; // radians about model Z, seconds in [0, duration)
};
struct SkinClip
{
	std::uint32_t id, samples;
	float duration;
	std::array<float, 16> modelToClip;
	std::array<float, 9> normalToCamera;
	std::array<float, 3> pivot;
	float radius;
	std::array<SkinFrame, 256> frames;
	// Uniformly spaced, sample-major local transforms; last wraps to first.
	std::vector<SkinTransform> tracks;
};
using SkinMatrix = std::array<float, 16>;
struct SkinPalette
{
	std::array<SkinMatrix, 32> positions{};
	std::array<std::array<float, 9>, 32> normals{};
	unsigned count = 0;
};
class SkinModel final
{
  public:
	// Returns no asset on failure. No mutable alias escapes successful loading.
	static std::shared_ptr<const SkinModel> decode(std::span<const std::uint8_t> bytes,
												   std::string &error);
	std::uint64_t identity() const { return generation; }
	unsigned logicalSize() const { return size; }
	unsigned vertices() const { return restGeometry.size() / 6; }
	const auto &rest() const { return restGeometry; } // xyz, normal xyz
	const auto &uv() const { return textureCoordinates; }
	const auto &indices() const { return triangles; }
	const auto &influences() const { return weights; }
	const auto &bones() const { return skeleton; }
	const auto &clips() const { return animations; }
	// Clip arguments index clips(); SkinClip::id is the exported semantic id.
	// Invalid requests fail without modifying output. Continuous time wraps in
	// either direction; gameplay uses paletteForFrame with its existing mapper.
	bool palette(unsigned clip, double time, float heading, SkinPalette &out) const;
	bool paletteForFrame(unsigned clip, unsigned frame, SkinPalette &out) const;
	// Reuses caller storage, never creates a per-pose mesh or serialized GSK.
	bool evaluate(unsigned clip, unsigned frame, std::vector<float> &out,
				  bool cameraSpace = true) const;

  private:
	SkinModel() = default;
	std::uint64_t generation = 0;
	unsigned size = 0;
	std::vector<float> restGeometry, textureCoordinates;
	std::vector<std::uint32_t> triangles;
	std::vector<SkinInfluence> weights;
	std::vector<SkinBone> skeleton;
	std::vector<SkinClip> animations;
};
} // namespace GAGCore
