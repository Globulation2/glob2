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
// GSB1: a blend-shape model fitted to baked clips. A frame is the mean mesh
// plus a weighted sum of shape vectors, for positions and for normals
// separately, turned about model Z by the frame's heading and taken into the
// clip's orthographic camera. Shape vectors are int16 with one float scale
// each. All clips share the geometry and shapes; each holds 256 frames.
struct SkinShapeClip
{
	std::uint32_t id;
	std::array<float, 16> modelToClip;
	std::array<float, 9> normalToCamera;
	std::array<float, 256> headings;
	std::vector<float> coefficients;       // 256 x shapes, frame-major
	std::vector<float> normalCoefficients; // 256 x normalShapes, frame-major
};
class SkinShapeModel final
{
  public:
	// Returns no model on failure. No mutable alias escapes successful loading.
	static std::shared_ptr<const SkinShapeModel> decode(std::span<const std::uint8_t> bytes,
														std::string &error);
	std::uint64_t identity() const { return generation; }
	unsigned logicalSize() const { return size; }
	unsigned vertices() const { return count; }
	unsigned shapes() const { return shapeCount; }
	unsigned normalShapes() const { return normalShapeCount; }
	const auto &uv() const { return textureCoordinates; }
	const auto &indices() const { return triangles; }
	const auto &mean() const { return meanPositions; } // xyz per vertex, model space
	const auto &normalMean() const { return meanNormals; }
	const auto &clips() const { return animations; }
	// Writes xyz, normal xyz per vertex for one frame into caller storage.
	// Invalid requests fail without modifying output.
	bool evaluate(unsigned clip, unsigned frame, std::vector<float> &out,
				  bool cameraSpace = true) const;

  private:
	SkinShapeModel() = default;
	std::uint64_t generation = 0;
	unsigned size = 0, count = 0, shapeCount = 0, normalShapeCount = 0;
	std::vector<float> textureCoordinates, meanPositions, meanNormals;
	std::vector<std::uint32_t> triangles;
	std::vector<float> shapeScales, normalScales;
	std::vector<std::int16_t> shapeDeltas, normalDeltas; // shape-major, xyz per vertex
	std::vector<SkinShapeClip> animations;
};
} // namespace GAGCore
