// SPDX-License-Identifier: GPL-3.0-or-later
#include <SkinShapeModel.h>
#include <SkinMesh.h>
#include "SkinAssetReader.h"
#include <atomic>
#include <cmath>
#include <set>

namespace GAGCore
{
namespace
{
using namespace SkinAsset;
std::atomic<std::uint64_t> nextGeneration{1};
constexpr unsigned HeaderBytes = 32, MaximumShapes = 128;
} // namespace
std::shared_ptr<const SkinShapeModel> SkinShapeModel::decode(std::span<const std::uint8_t> bytes,
															 std::string &error)
{
	error.clear();
	try
	{
		require(bytes.size() >= HeaderBytes && bytes.size() <= MaximumBytes, "invalid shape size");
		Reader r{bytes};
		require(r.word() == 0x31425347, "unsupported shape format");
		const auto vertices = r.word(), indices = r.word(), shapes = r.word(),
				   normalShapes = r.word(), clips = r.word(), logical = r.word(),
				   payload = r.word();
		require(vertices >= 3 && vertices <= MaximumVertices && indices >= 3 &&
					indices <= MaximumIndices && indices % 3 == 0 && shapes >= 1 &&
					shapes <= MaximumShapes && normalShapes >= 1 && normalShapes <= MaximumShapes &&
					clips >= 1 && clips <= MaximumClips && logical >= 1 &&
					logical <= MaximumLogicalSize && payload == bytes.size() - HeaderBytes,
				"invalid shape dimensions");
		const std::uint64_t expected =
			HeaderBytes + vertices * 8ULL + indices * 4ULL + vertices * 24ULL +
			(shapes + normalShapes) * (4ULL + vertices * 6ULL) +
			clips * (8ULL + 64 + 36 + SkinClipFrames * 4ULL +
					 SkinClipFrames * 4ULL * (shapes + normalShapes));
		require(expected == bytes.size(), "shape asset length mismatch");
		auto model = std::shared_ptr<SkinShapeModel>(new SkinShapeModel);
		model->size = logical;
		model->count = vertices;
		model->shapeCount = shapes;
		model->normalShapeCount = normalShapes;
		model->textureCoordinates.resize(vertices * 2);
		for (auto &uv : model->textureCoordinates)
		{
			uv = r.scalar();
			require(uv >= 0 && uv <= 1, "invalid shape UV");
		}
		model->triangles.resize(indices);
		for (auto &v : model->triangles)
		{
			v = r.word();
			require(v < vertices, "invalid shape index");
		}
		model->meanPositions.resize(vertices * 3);
		for (auto &v : model->meanPositions)
			v = r.scalar();
		model->meanNormals.resize(vertices * 3);
		for (auto &v : model->meanNormals)
			v = r.scalar();
		auto readShapes = [&](unsigned total, std::vector<float> &scales,
							  std::vector<std::int16_t> &deltas)
		{
			scales.resize(total);
			deltas.resize(std::size_t(total) * vertices * 3);
			for (unsigned s = 0; s < total; ++s)
			{
				scales[s] = r.scalar();
				require(scales[s] > 0, "invalid shape scale");
				for (std::size_t k = 0; k < std::size_t(vertices) * 3; ++k)
					deltas[std::size_t(s) * vertices * 3 + k] = r.half();
			}
		};
		readShapes(shapes, model->shapeScales, model->shapeDeltas);
		readShapes(normalShapes, model->normalScales, model->normalDeltas);
		std::set<std::uint32_t> ids;
		for (unsigned c = 0; c < clips; ++c)
		{
			SkinShapeClip clip;
			clip.id = r.word();
			require(ids.insert(clip.id).second, "duplicate shape clip");
			require(r.word() == SkinClipFrames, "unsupported shape clip frames");
			r.read(clip.modelToClip);
			r.read(clip.normalToCamera);
			requireCamera(clip.modelToClip, clip.normalToCamera);
			for (auto &h : clip.headings)
			{
				h = r.scalar();
				require(std::abs(h) <= MaximumHeading, "invalid shape heading");
			}
			clip.coefficients.resize(std::size_t(SkinClipFrames) * shapes);
			for (auto &v : clip.coefficients)
				v = r.scalar();
			clip.normalCoefficients.resize(std::size_t(SkinClipFrames) * normalShapes);
			for (auto &v : clip.normalCoefficients)
				v = r.scalar();
			model->animations.push_back(std::move(clip));
		}
		require(r.finished(), "trailing shape data");
		model->generation = nextGeneration.fetch_add(1, std::memory_order_relaxed);
		return model;
	}
	catch (const std::exception &failure)
	{
		error = failure.what();
		return nullptr;
	}
}
bool SkinShapeModel::evaluate(unsigned clipIndex, unsigned frame, std::vector<float> &out,
							  bool cameraSpace) const
{
	if (!generation || clipIndex >= animations.size() || frame >= SkinClipFrames)
		return false;
	const auto &clip = animations[clipIndex];
	out.resize(std::size_t(count) * 6);
	for (unsigned v = 0; v < count; ++v)
	{
		float *p = out.data() + std::size_t(v) * 6;
		for (unsigned k = 0; k < 3; ++k)
		{
			p[k] = meanPositions[v * 3 + k];
			p[3 + k] = meanNormals[v * 3 + k];
		}
	}
	// Positions and normals have separate bases; each shape adds its scaled
	// int16 deltas to every vertex of the mean.
	auto blend = [&](unsigned total, const float *weights, const std::vector<float> &scales,
					 const std::vector<std::int16_t> &deltas, unsigned component)
	{
		for (unsigned s = 0; s < total; ++s)
		{
			const float scale = weights[s] * scales[s];
			const std::int16_t *delta = deltas.data() + std::size_t(s) * count * 3;
			for (unsigned v = 0; v < count; ++v)
				for (unsigned k = 0; k < 3; ++k)
					out[std::size_t(v) * 6 + component + k] += scale * float(delta[v * 3 + k]);
		}
	};
	blend(shapeCount, clip.coefficients.data() + std::size_t(frame) * shapeCount, shapeScales,
		  shapeDeltas, 0);
	blend(normalShapeCount, clip.normalCoefficients.data() + std::size_t(frame) * normalShapeCount,
		  normalScales, normalDeltas, 3);
	// Turn the blended model about Z by the frame's heading, then apply the
	// clip camera when a camera-space pose is wanted.
	const float heading = clip.headings[frame];
	const float c = std::cos(heading), s = std::sin(heading);
	const auto &m = clip.modelToClip;
	const auto &n = clip.normalToCamera;
	for (unsigned v = 0; v < count; ++v)
	{
		float *p = out.data() + std::size_t(v) * 6;
		const float x = c * p[0] - s * p[1], y = s * p[0] + c * p[1], z = p[2];
		const float nx = c * p[3] - s * p[4], ny = s * p[3] + c * p[4], nz = p[5];
		if (cameraSpace)
		{
			for (unsigned row = 0; row < 3; ++row)
				p[row] = m[row * 4] * x + m[row * 4 + 1] * y + m[row * 4 + 2] * z + m[row * 4 + 3];
			for (unsigned row = 0; row < 3; ++row)
				p[3 + row] = n[row * 3] * nx + n[row * 3 + 1] * ny + n[row * 3 + 2] * nz;
		}
		else
		{
			p[0] = x;
			p[1] = y;
			p[2] = z;
			p[3] = nx;
			p[4] = ny;
			p[5] = nz;
		}
		normalize(p + 3);
	}
	return true;
}
} // namespace GAGCore
