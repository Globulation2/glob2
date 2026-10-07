// SPDX-License-Identifier: GPL-3.0-or-later
#include <SkinModel.h>
#include <SkinMesh.h>
#include "SkinAssetReader.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace GAGCore
{
namespace
{
using namespace SkinAsset;
std::atomic<std::uint64_t> nextGeneration{1};
constexpr unsigned HeaderBytes = 28, MaximumBones = 32, MaximumSamples = 256;
constexpr double MinimumScale = double(0.0001f);
constexpr double MinimumDuration = double(0.0001f);
constexpr unsigned NoParent = 0xffffffffu;
// Serialized sizes: a vertex (position, normal, UV, four bones, four weights),
// a bone (parent, rest, inverse bind), a clip header and a track key.
constexpr unsigned VertexBytes = 64, BoneBytes = 68, ClipHeaderBytes = 2176,
				   TransformBytes = 32;

SkinTransform readTransform(Reader &r)
{
	SkinTransform t;
	r.read(t.translation);
	r.read(t.rotation);
	t.scale = r.scalar();
	double norm = 0;
	for (float v : t.rotation)
		norm += double(v) * v;
	require(std::abs(norm - 1) <= Tolerance && t.scale >= MinimumScale &&
				t.scale <= MaximumScalar,
			"invalid rig transform");
	for (auto &v : t.rotation)
		v = float(v / std::sqrt(norm));
	return t;
}
template <typename Scalar = float> std::array<Scalar, 16> matrix(const SkinTransform &t)
{
	const Scalar x = t.rotation[0], y = t.rotation[1], z = t.rotation[2], w = t.rotation[3];
	const Scalar s = t.scale;
	return {(1 - 2 * (y * y + z * z)) * s,
			2 * (x * y - z * w) * s,
			2 * (x * z + y * w) * s,
			t.translation[0],
			2 * (x * y + z * w) * s,
			(1 - 2 * (x * x + z * z)) * s,
			2 * (y * z - x * w) * s,
			t.translation[1],
			2 * (x * z - y * w) * s,
			2 * (y * z + x * w) * s,
			(1 - 2 * (x * x + y * y)) * s,
			t.translation[2],
			0,
			0,
			0,
			1};
}
template <typename Scalar>
std::array<Scalar, 16> multiply(const std::array<Scalar, 16> &a, const std::array<Scalar, 16> &b)
{
	std::array<Scalar, 16> r{};
	for (unsigned y = 0; y < 4; ++y)
		for (unsigned x = 0; x < 4; ++x)
			for (unsigned k = 0; k < 4; ++k)
				r[y * 4 + x] += a[y * 4 + k] * b[k * 4 + x];
	return r;
}
// Linear translation and scale, shortest-arc spherical rotation (linear below
// the 0.9995 cosine threshold, where the arc is too short to matter).
SkinTransform interpolate(const SkinTransform &a, const SkinTransform &b, float f)
{
	SkinTransform r;
	for (unsigned k = 0; k < 3; ++k)
		r.translation[k] = a.translation[k] * (1 - f) + b.translation[k] * f;
	r.scale = a.scale * (1 - f) + b.scale * f;
	float dot = 0;
	for (unsigned k = 0; k < 4; ++k)
		dot += a.rotation[k] * b.rotation[k];
	const float sign = dot < 0 ? -1.f : 1.f;
	dot = std::clamp(std::abs(dot), 0.f, 1.f);
	float left = 1 - f, right = f;
	if (dot < 0.9995f)
	{
		const float angle = std::acos(dot), divisor = std::sin(angle);
		left = std::sin((1 - f) * angle) / divisor;
		right = std::sin(f * angle) / divisor;
	}
	float length = 0;
	for (unsigned k = 0; k < 4; ++k)
	{
		r.rotation[k] = left * a.rotation[k] + right * sign * b.rotation[k];
		length += r.rotation[k] * r.rotation[k];
	}
	for (auto &v : r.rotation)
		v /= std::sqrt(length);
	return r;
}
} // namespace
std::shared_ptr<const SkinModel> SkinModel::decode(std::span<const std::uint8_t> bytes,
												   std::string &error)
{
	error.clear();
	try
	{
		require(bytes.size() >= HeaderBytes && bytes.size() <= MaximumBytes, "invalid rig size");
		Reader r{bytes};
		require(r.word() == 0x31525347, "unsupported rig format");
		const auto vertices = r.word(), indices = r.word(), bones = r.word(), clips = r.word(),
				   logical = r.word(), payload = r.word();
		require(vertices >= 3 && vertices <= MaximumVertices && indices >= 3 &&
					indices <= MaximumIndices && indices % 3 == 0 && bones >= 1 &&
					bones <= MaximumBones && clips >= 1 && clips <= MaximumClips && logical >= 1 &&
					logical <= MaximumLogicalSize && payload == bytes.size() - HeaderBytes,
				"invalid rig dimensions");
		// Fixed geometry and every clip header must fit before allocating.
		require(HeaderBytes + vertices * std::uint64_t(VertexBytes) + indices * 4ULL +
					bones * std::uint64_t(BoneBytes) + clips * std::uint64_t(ClipHeaderBytes) <=
					bytes.size(),
				"truncated rig geometry");
		auto model = std::shared_ptr<SkinModel>(new SkinModel);
		model->size = logical;
		model->restGeometry.resize(vertices * 6);
		model->textureCoordinates.resize(vertices * 2);
		model->weights.resize(vertices);
		for (unsigned v = 0; v < vertices; ++v)
		{
			for (unsigned k = 0; k < 6; ++k)
				model->restGeometry[v * 6 + k] = r.scalar();
			const float *n = model->restGeometry.data() + v * 6 + 3;
			require(std::abs(double(n[0]) * n[0] + double(n[1]) * n[1] + double(n[2]) * n[2] - 1) <
						Tolerance,
					"invalid rig normal");
			for (unsigned k = 0; k < 2; ++k)
			{
				auto &uv = model->textureCoordinates[v * 2 + k];
				uv = r.scalar();
				require(uv >= 0 && uv <= 1, "invalid rig UV");
			}
			auto &influence = model->weights[v];
			for (auto &b : influence.bones)
			{
				b = r.word();
				require(b < bones, "invalid rig influence bone");
			}
			double sum = 0;
			for (auto &w : influence.weights)
			{
				w = r.scalar();
				require(w >= 0 && w <= 1, "invalid rig weight");
				sum += w;
			}
			require(std::abs(sum - 1) < Tolerance, "unnormalized rig weights");
			for (auto &w : influence.weights)
				w = float(w / sum);
		}
		model->triangles.resize(indices);
		for (auto &v : model->triangles)
		{
			v = r.word();
			require(v < vertices, "invalid rig index");
		}
		std::array<std::array<double, 16>, MaximumBones> global{};
		for (unsigned b = 0; b < bones; ++b)
		{
			SkinBone bone;
			bone.parent = r.word();
			require(bone.parent == NoParent || bone.parent < b, "invalid rig hierarchy");
			bone.rest = readTransform(r);
			bone.inverseBind = readTransform(r);
			global[b] = matrix<double>(bone.rest);
			if (bone.parent != NoParent)
				global[b] = multiply(global[bone.parent], global[b]);
			const auto identity = multiply(global[b], matrix<double>(bone.inverseBind));
			for (unsigned i = 0; i < 16; ++i)
				require(std::isfinite(identity[i]) &&
							std::abs(identity[i] - (i % 5 == 0 ? 1. : 0.)) < 0.002,
						"invalid rig inverse bind");
			model->skeleton.push_back(bone);
		}
		for (unsigned c = 0; c < clips; ++c)
		{
			SkinClip clip;
			clip.id = r.word();
			clip.samples = r.word();
			clip.duration = r.scalar();
			require(clip.samples >= 1 && clip.samples <= MaximumSamples &&
						clip.duration >= MinimumDuration,
					"invalid rig track dimensions");
			for (const auto &other : model->animations)
				require(other.id != clip.id, "duplicate rig clip");
			r.read(clip.modelToClip);
			r.read(clip.normalToCamera);
			r.read(clip.pivot);
			clip.radius = r.scalar();
			require(clip.radius > 0, "invalid rig radius");
			requireCamera(clip.modelToClip, clip.normalToCamera);
			for (auto &frame : clip.frames)
			{
				frame.heading = r.scalar();
				frame.time = r.scalar();
				require(std::abs(frame.heading) <= MaximumHeading && frame.time >= 0 &&
							frame.time < clip.duration,
						"invalid rig frame mapping");
			}
			require(std::uint64_t(clip.samples) * bones * TransformBytes <= bytes.size() - r.at,
					"truncated rig tracks");
			clip.tracks.reserve(clip.samples * bones);
			for (unsigned i = 0; i < clip.samples * bones; ++i)
				clip.tracks.push_back(readTransform(r));
			// Bound every interpolated hierarchy, including combinations between
			// keys, before publishing. Prevent overflow from scale products.
			std::array<double, MaximumBones> lo{}, hi{};
			for (unsigned b = 0; b < bones; ++b)
			{
				lo[b] = MaximumScalar;
				hi[b] = 0;
				for (unsigned i = 0; i < clip.samples; ++i)
				{
					const auto s = clip.tracks[i * bones + b].scale;
					lo[b] = std::min(lo[b], double(s));
					hi[b] = std::max(hi[b], double(s));
				}
				const auto parent = model->skeleton[b].parent;
				if (parent != NoParent)
				{
					lo[b] *= lo[parent];
					hi[b] *= hi[parent];
				}
				require(lo[b] >= MinimumScale && hi[b] <= MaximumScalar,
						"unbounded rig hierarchy scale");
				const auto inverse = model->skeleton[b].inverseBind.scale;
				require(lo[b] * inverse >= MinimumScale && hi[b] * inverse <= MaximumScalar,
						"unbounded rig deformation scale");
			}
			model->animations.push_back(std::move(clip));
		}
		require(r.finished(), "rig payload length mismatch");
		model->generation = nextGeneration.fetch_add(1, std::memory_order_relaxed);
		return model;
	}
	catch (const std::exception &e)
	{
		error = e.what();
		return {};
	}
}
bool SkinModel::palette(unsigned clipIndex, double time, float heading, SkinPalette &out) const
{
	if (clipIndex >= animations.size() || !std::isfinite(time) || !std::isfinite(heading))
		return false;
	const auto &clip = animations[clipIndex];
	double wrapped = std::fmod(time, double(clip.duration));
	if (wrapped < 0)
		wrapped += clip.duration;
	const double sample = wrapped / clip.duration * clip.samples;
	const unsigned a = std::min(unsigned(sample), clip.samples - 1), b = (a + 1) % clip.samples;
	const float fraction = sample - a;
	std::array<SkinMatrix, MaximumBones> global;
	SkinPalette result;
	result.count = skeleton.size();
	// Heading turns the posed model about the clip pivot's vertical axis.
	const float c = std::cos(heading), s = std::sin(heading);
	const auto &p = clip.pivot;
	const SkinMatrix turn{
		c, -s, 0, p[0] - c * p[0] + s * p[1], s, c, 0, p[1] - s * p[0] - c * p[1], 0, 0, 1, 0, 0,
		0, 0,  1};
	for (unsigned i = 0; i < skeleton.size(); ++i)
	{
		global[i] = matrix(interpolate(clip.tracks[a * skeleton.size() + i],
									   clip.tracks[b * skeleton.size() + i], fraction));
		if (skeleton[i].parent != NoParent)
			global[i] = multiply(global[skeleton[i].parent], global[i]);
		auto &m = result.positions[i];
		m = multiply(turn, multiply(global[i], matrix(skeleton[i].inverseBind)));
		// Uniform scale: the normal matrix is the rotation divided by scale.
		const float squaredScale = m[0] * m[0] + m[4] * m[4] + m[8] * m[8];
		for (unsigned y = 0; y < 3; ++y)
			for (unsigned x = 0; x < 3; ++x)
				result.normals[i][y * 3 + x] = m[y * 4 + x] / squaredScale;
	}
	out = result;
	return true;
}
bool SkinModel::paletteForFrame(unsigned clip, unsigned frame, SkinPalette &out) const
{
	if (clip >= animations.size() || frame >= SkinClipFrames)
		return false;
	const auto &mapping = animations[clip].frames[frame];
	return palette(clip, mapping.time, mapping.heading, out);
}
bool SkinModel::evaluate(unsigned clip, unsigned frame, std::vector<float> &out,
						 bool cameraSpace) const
{
	SkinPalette pose;
	if (!paletteForFrame(clip, frame, pose))
		return false;
	out.resize(restGeometry.size());
	const auto &camera = animations[clip];
	for (unsigned v = 0; v < vertices(); ++v)
	{
		float p[3]{}, n[3]{};
		const auto *rest = restGeometry.data() + v * 6;
		for (unsigned influence = 0; influence < 4; ++influence)
		{
			const auto bone = weights[v].bones[influence];
			const auto w = weights[v].weights[influence];
			const auto &m = pose.positions[bone];
			const auto &normal = pose.normals[bone];
			for (unsigned k = 0; k < 3; ++k)
			{
				p[k] += w * (m[k * 4] * rest[0] + m[k * 4 + 1] * rest[1] + m[k * 4 + 2] * rest[2] +
							 m[k * 4 + 3]);
				n[k] += w * (normal[k * 3] * rest[3] + normal[k * 3 + 1] * rest[4] +
							 normal[k * 3 + 2] * rest[5]);
			}
		}
		normalize(n);
		for (unsigned k = 0; k < 3; ++k)
		{
			const auto &m = camera.modelToClip;
			const auto &normal = camera.normalToCamera;
			out[v * 6 + k] = cameraSpace ? m[k * 4] * p[0] + m[k * 4 + 1] * p[1] +
											   m[k * 4 + 2] * p[2] + m[k * 4 + 3]
										 : p[k];
			out[v * 6 + 3 + k] = cameraSpace ? normal[k * 3] * n[0] + normal[k * 3 + 1] * n[1] +
												   normal[k * 3 + 2] * n[2]
											 : n[k];
		}
		normalize(out.data() + v * 6 + 3);
	}
	return true;
}
} // namespace GAGCore
