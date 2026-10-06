// SPDX-License-Identifier: GPL-3.0-or-later
#include <SkinModel.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace GAGCore
{
namespace
{
std::atomic<std::uint64_t> nextGeneration{1};
// Validation arithmetic is binary64 in both native and Studio decoders. Limits
// on serialized scalars use their binary32 representation, including endpoints.
constexpr double Tolerance = 0.0001;
constexpr double MinimumScale = double(0.0001f);
constexpr double MinimumDuration = double(0.0001f);
constexpr double MaximumHeading = double(6.283186f);
constexpr unsigned NoParent = 0xffffffffu;
struct Reader
{
	std::span<const std::uint8_t> bytes;
	std::size_t at = 0;
	std::uint32_t word()
	{
		if (bytes.size() - at < 4)
			throw std::runtime_error("truncated rig payload");
		const auto *p = bytes.data() + at;
		at += 4;
		return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 |
			   std::uint32_t(p[3]) << 24;
	}
	float scalar(float bound = 10000)
	{
		const float v = std::bit_cast<float>(word());
		if (!std::isfinite(v) || std::abs(v) > bound)
			throw std::runtime_error("invalid rig scalar");
		return v;
	}
	template <std::size_t N> void read(std::array<float, N> &a)
	{
		for (auto &v : a)
			v = scalar();
	}
	SkinTransform transform()
	{
		SkinTransform t;
		read(t.translation);
		read(t.rotation);
		t.scale = scalar();
		double norm = 0;
		for (float v : t.rotation)
			norm += double(v) * v;
		if (std::abs(norm - 1) > Tolerance || t.scale < MinimumScale || t.scale > 10000)
			throw std::runtime_error("invalid rig transform");
		for (auto &v : t.rotation)
			v = float(v / std::sqrt(norm));
		return t;
	}
};
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
std::shared_ptr<const SkinModel> SkinModel::decode(std::span<const std::uint8_t> bytes,
												   std::string &error)
{
	error.clear();
	try
	{
		require(bytes.size() >= 28 && bytes.size() <= 16 * 1024 * 1024, "invalid rig size");
		Reader r{bytes};
		require(r.word() == 0x31525347, "unsupported rig format");
		const auto vertices = r.word(), indices = r.word(), bones = r.word(), clips = r.word(),
				   logical = r.word(), payload = r.word();
		require(vertices >= 3 && vertices <= 8192 && indices >= 3 && indices <= 49152 &&
					indices % 3 == 0 && bones >= 1 && bones <= 32 && clips >= 1 && clips <= 8 &&
					logical >= 1 && logical <= 128 && payload == bytes.size() - 28,
				"invalid rig dimensions");
		// Fixed geometry and every clip header must fit before allocating.
		require(28ULL + vertices * 64ULL + indices * 4ULL + bones * 68ULL + clips * 2176ULL <=
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
		std::array<std::array<double, 16>, 32> global{};
		for (unsigned b = 0; b < bones; ++b)
		{
			SkinBone bone;
			bone.parent = r.word();
			require(bone.parent == NoParent || bone.parent < b, "invalid rig hierarchy");
			bone.rest = r.transform();
			bone.inverseBind = r.transform();
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
			require(clip.samples >= 1 && clip.samples <= 256 && clip.duration >= MinimumDuration,
					"invalid rig track dimensions");
			for (const auto &other : model->animations)
				require(other.id != clip.id, "duplicate rig clip");
			r.read(clip.modelToClip);
			r.read(clip.normalToCamera);
			r.read(clip.pivot);
			clip.radius = r.scalar();
			require(clip.radius > 0, "invalid rig radius");
			const auto &m = clip.modelToClip;
			require(m[12] == 0 && m[13] == 0 && m[14] == 0 && m[15] == 1, "invalid rig camera");
			const double determinant = double(m[0]) * (double(m[5]) * m[10] - double(m[6]) * m[9]) -
									   double(m[1]) * (double(m[4]) * m[10] - double(m[6]) * m[8]) +
									   double(m[2]) * (double(m[4]) * m[9] - double(m[5]) * m[8]);
			require(std::abs(determinant) > 1e-12, "singular rig camera");
			const auto &n = clip.normalToCamera;
			for (unsigned i = 0; i < 3; ++i)
				for (unsigned j = 0; j < 3; ++j)
				{
					double dot = 0;
					for (unsigned k = 0; k < 3; ++k)
						dot += double(n[i * 3 + k]) * n[j * 3 + k];
					require(std::abs(dot - (i == j ? 1. : 0.)) < Tolerance,
							"invalid rig normal camera");
				}
			for (auto &frame : clip.frames)
			{
				frame.heading = r.scalar();
				frame.time = r.scalar();
				require(std::abs(frame.heading) <= MaximumHeading && frame.time >= 0 &&
							frame.time < clip.duration,
						"invalid rig frame mapping");
			}
			require(std::uint64_t(clip.samples) * bones * 32 <= bytes.size() - r.at,
					"truncated rig tracks");
			clip.tracks.reserve(clip.samples * bones);
			std::array<double, 32> lo{}, hi{};
			for (unsigned i = 0; i < clip.samples * bones; ++i)
				clip.tracks.push_back(r.transform());
			// Bound every interpolated hierarchy, including combinations between
			// keys, before publishing. Prevent overflow from scale products.
			for (unsigned b = 0; b < bones; ++b)
			{
				lo[b] = 10000;
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
				require(lo[b] >= MinimumScale && hi[b] <= 10000, "unbounded rig hierarchy scale");
				const auto inverse = model->skeleton[b].inverseBind.scale;
				require(lo[b] * inverse >= MinimumScale && hi[b] * inverse <= 10000,
						"unbounded rig deformation scale");
			}
			model->animations.push_back(std::move(clip));
		}
		require(r.at == bytes.size(), "rig payload length mismatch");
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
	std::array<SkinMatrix, 32> global;
	SkinPalette result;
	result.count = skeleton.size();
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
	if (clip >= animations.size() || frame >= 256)
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
