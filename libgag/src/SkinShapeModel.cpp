// SPDX-License-Identifier: GPL-3.0-or-later
#include <SkinShapeModel.h>
#include <atomic>
#include <bit>
#include <cmath>
#include <set>
#include <stdexcept>

namespace GAGCore
{
namespace
{
std::atomic<std::uint64_t> nextGeneration{1};
constexpr double Tolerance = 0.0001;
constexpr float MaximumHeading = 6.283186f;
constexpr unsigned Frames = 256, MaximumShapes = 128;
struct Reader
{
	std::span<const std::uint8_t> bytes;
	std::size_t at = 0;
	std::uint32_t word()
	{
		if (at + 4 > bytes.size())
			throw std::runtime_error("truncated shape asset");
		const std::uint32_t v = std::uint32_t(bytes[at]) | std::uint32_t(bytes[at + 1]) << 8 |
								std::uint32_t(bytes[at + 2]) << 16 |
								std::uint32_t(bytes[at + 3]) << 24;
		at += 4;
		return v;
	}
	std::int16_t half()
	{
		if (at + 2 > bytes.size())
			throw std::runtime_error("truncated shape asset");
		const std::uint16_t v = std::uint16_t(bytes[at]) | std::uint16_t(bytes[at + 1]) << 8;
		at += 2;
		return std::bit_cast<std::int16_t>(v);
	}
	float scalar()
	{
		const float v = std::bit_cast<float>(word());
		if (!std::isfinite(v) || std::abs(v) > 10000)
			throw std::runtime_error("invalid shape scalar");
		return v;
	}
};
void require(bool ok, const char *error)
{
	if (!ok)
		throw std::runtime_error(error);
}
void normalize(float *n)
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
} // namespace
std::shared_ptr<const SkinShapeModel> SkinShapeModel::decode(std::span<const std::uint8_t> bytes,
															 std::string &error)
{
	error.clear();
	try
	{
		require(bytes.size() >= 32 && bytes.size() <= 16 * 1024 * 1024, "invalid shape size");
		Reader r{bytes};
		require(r.word() == 0x31425347, "unsupported shape format");
		const auto vertices = r.word(), indices = r.word(), shapes = r.word(),
				   normalShapes = r.word(), clips = r.word(), logical = r.word(),
				   payload = r.word();
		require(vertices >= 3 && vertices <= 8192 && indices >= 3 && indices <= 49152 &&
					indices % 3 == 0 && shapes >= 1 && shapes <= MaximumShapes &&
					normalShapes >= 1 && normalShapes <= MaximumShapes && clips >= 1 &&
					clips <= 8 && logical >= 1 && logical <= 128 && payload == bytes.size() - 32,
				"invalid shape dimensions");
		const std::uint64_t expected =
			32ULL + vertices * 8ULL + indices * 4ULL + vertices * 24ULL +
			(shapes + normalShapes) * (4ULL + vertices * 6ULL) +
			clips * (8ULL + 64 + 36 + Frames * 4ULL + Frames * 4ULL * (shapes + normalShapes));
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
			require(r.word() == Frames, "unsupported shape clip frames");
			for (auto &v : clip.modelToClip)
				v = r.scalar();
			const auto &m = clip.modelToClip;
			require(m[12] == 0 && m[13] == 0 && m[14] == 0 && m[15] == 1, "invalid shape camera");
			const double determinant = double(m[0]) * (double(m[5]) * m[10] - double(m[6]) * m[9]) -
									   double(m[1]) * (double(m[4]) * m[10] - double(m[6]) * m[8]) +
									   double(m[2]) * (double(m[4]) * m[9] - double(m[5]) * m[8]);
			require(std::abs(determinant) > 1e-12, "invalid shape camera");
			for (auto &v : clip.normalToCamera)
				v = r.scalar();
			for (unsigned i = 0; i < 3; ++i)
				for (unsigned j = 0; j < 3; ++j)
				{
					double dot = 0;
					for (unsigned k = 0; k < 3; ++k)
						dot += double(clip.normalToCamera[i * 3 + k]) * clip.normalToCamera[j * 3 + k];
					require(std::abs(dot - (i == j ? 1 : 0)) < Tolerance, "invalid shape normal camera");
				}
			for (auto &h : clip.headings)
			{
				h = r.scalar();
				require(std::abs(h) <= MaximumHeading, "invalid shape heading");
			}
			clip.coefficients.resize(std::size_t(Frames) * shapes);
			for (auto &v : clip.coefficients)
				v = r.scalar();
			clip.normalCoefficients.resize(std::size_t(Frames) * normalShapes);
			for (auto &v : clip.normalCoefficients)
				v = r.scalar();
			model->animations.push_back(std::move(clip));
		}
		require(r.at == bytes.size(), "trailing shape data");
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
	if (!generation || clipIndex >= animations.size() || frame >= Frames)
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
	const float *weights = clip.coefficients.data() + std::size_t(frame) * shapeCount;
	for (unsigned s = 0; s < shapeCount; ++s)
	{
		const float scale = weights[s] * shapeScales[s];
		const std::int16_t *delta = shapeDeltas.data() + std::size_t(s) * count * 3;
		for (unsigned v = 0; v < count; ++v)
			for (unsigned k = 0; k < 3; ++k)
				out[std::size_t(v) * 6 + k] += scale * float(delta[v * 3 + k]);
	}
	const float *normalWeights = clip.normalCoefficients.data() + std::size_t(frame) * normalShapeCount;
	for (unsigned s = 0; s < normalShapeCount; ++s)
	{
		const float scale = normalWeights[s] * normalScales[s];
		const std::int16_t *delta = normalDeltas.data() + std::size_t(s) * count * 3;
		for (unsigned v = 0; v < count; ++v)
			for (unsigned k = 0; k < 3; ++k)
				out[std::size_t(v) * 6 + 3 + k] += scale * float(delta[v * 3 + k]);
	}
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
