// SPDX-License-Identifier: GPL-3.0-or-later
#include <SkinMesh.h>
#include <SkinModel.h>
#include <FileManager.h>
#include <SkinShapeModel.h>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <StreamBackend.h>
#include <limits>
#include <utility>
#include <Toolkit.h>
#include <filesystem>
#include <stdexcept>

namespace GAGCore
{
namespace { std::atomic<std::uint64_t> nextIdentity{1}; }
SkinMesh SkinMesh::fromModel(std::shared_ptr<const SkinModel> model, unsigned clip)
{
	if (!model || clip >= model->clips().size())
		return {};
	SkinMesh mesh;
	mesh.identity = nextIdentity.fetch_add(1, std::memory_order_relaxed);
	mesh.model = std::move(model);
	mesh.clip = clip;
	mesh.vertices = mesh.model->vertices();
	mesh.frames = SkinClipFrames;
	mesh.logicalSize = mesh.model->logicalSize();
	mesh.uv = mesh.model->uv();
	mesh.indices = mesh.model->indices();
	return mesh;
}
SkinMesh SkinMesh::fromShapes(std::shared_ptr<const SkinShapeModel> shapes, unsigned clip)
{
	if (!shapes || clip >= shapes->clips().size())
		return {};
	SkinMesh mesh;
	mesh.identity = nextIdentity.fetch_add(1, std::memory_order_relaxed);
	mesh.shapes = std::move(shapes);
	mesh.clip = clip;
	mesh.vertices = mesh.shapes->vertices();
	mesh.frames = SkinClipFrames;
	mesh.logicalSize = mesh.shapes->logicalSize();
	mesh.uv = mesh.shapes->uv();
	mesh.indices = mesh.shapes->indices();
	return mesh;
}
bool SkinMesh::evaluate(unsigned frame, std::vector<float> &output) const
{
	if (!identity || frame >= frames)
		return false;
	if (model)
		return model->evaluate(clip, frame, output);
	if (shapes)
		return shapes->evaluate(clip, frame, output);
	// Baked poses may have been assembled by hand: reject incomplete storage
	// as well as invalid frame requests, and keep the output on failure.
	const auto count = std::size_t(vertices) * 6;
	if (!count || poses.size() / count < frames)
		return false;
	const auto begin = poses.begin() + std::size_t(frame) * count;
	output.assign(begin, begin + count);
	return true;
}
AssetLoader::Handle<SkinMesh> requestSkinMesh(AssetLoader& loader, const std::string& path)
{
    auto bytes = loader.requestBytes(path);
    return loader.requestEstimated<SkinMesh>("mesh:" + path, {bytes.dependency()}, [bytes, path] {
        auto input = bytes.get();
        MemoryStreamBackend stream(input->data(), input->size());
        auto mesh = std::make_shared<SkinMesh>(); std::string error;
        if (!mesh->load(stream, error)) throw std::runtime_error(error);
        std::filesystem::path sidecar(path);
        sidecar.replace_extension(".guv");
        auto *files = Toolkit::getFileManager();
        std::unique_ptr<StreamBackend> detail(files ? files->openInputStreamBackend(sidecar.string())
            : new FileStreamBackend(std::fopen(sidecar.string().c_str(), "rb")));
        if (detail->isValid() && !mesh->loadDetailUV(*detail, error)) throw std::runtime_error(error);
        return mesh;
    }, [bytes] { return bytes.get()->size() * 2; });
}
bool skinRigsDisabled()
{
	const char *mode = std::getenv("GLOB2_SKIN_RIGS");
	return mode && std::string(mode) == "0";
}
std::string skinClipFile(const std::string &clip, bool baked)
{
	const bool unit = clip.rfind("worker-", 0) == 0 || clip.rfind("warrior-", 0) == 0 ||
					  clip.rfind("explorer-", 0) == 0;
	if (baked || !unit)
		return clip + ".gsk";
	return clip + (clip.rfind("explorer-", 0) == 0 ? ".gsr" : ".gsb");
}
bool SkinMesh::load(const std::string &path, std::string &error)
{
    if (Toolkit::getFileManager()) {
        auto request = requestSkinMesh(Toolkit::assets(), std::filesystem::absolute(path).string());
        auto mesh = Toolkit::assets().wait(request);
        if (!mesh) { error = request.error(); return false; }
        *this = *mesh; error.clear(); return true;
    }
    FileStreamBackend input(std::fopen(path.c_str(), "rb"));
    SkinMesh candidate;
    if (!candidate.load(input, error)) return false;
    std::filesystem::path sidecar(path);
    sidecar.replace_extension(".guv");
    FileStreamBackend detail(std::fopen(sidecar.string().c_str(), "rb"));
    if (detail.isValid() && !candidate.loadDetailUV(detail, error)) return false;
    *this = std::move(candidate);
    return true;
}
bool SkinMesh::load(StreamBackend &input, std::string &error)
{
    error.clear();
    auto fail = [&](const char *why) { error = why; return false; };
    if (!input.isValid()) return fail("cannot open skin mesh");
    input.seekFromEnd(0);
    const auto length = input.getPosition();
    if (length < 20 || length > 64 * 1024 * 1024) return fail("invalid skin mesh size");
    input.seekFromStart(0);
    std::array<char, 4> magic{};
    bool complete = input.readExact(magic.data(), 4);
	// Rigs and blend shapes are decoded from the whole file by their own
	// bounded decoders; only baked GSK1 streams through the reader below.
	const bool rig = magic == std::array<char, 4>{'G', 'S', 'R', '1'};
	if (rig || magic == std::array<char, 4>{'G', 'S', 'B', '1'})
	{
		if (length > 16 * 1024 * 1024)
			return fail("invalid skin asset size");
		input.seekFromStart(0);
		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
		if (!input.readExact(bytes.data(), bytes.size()))
			return fail("truncated skin asset");
		SkinMesh decoded = rig ? fromModel(SkinModel::decode(bytes, error), 0)
							   : fromShapes(SkinShapeModel::decode(bytes, error), 0);
		if (!decoded.identity)
			return false;
		*this = std::move(decoded);
		return true;
	}
    if (magic != std::array<char, 4>{'G','S','K','1'}) return fail("unsupported skin mesh format");
    auto word = [&]() {
        std::array<unsigned char, 4> b{};
        complete = input.readExact(b.data(), 4) && complete;
        return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 |
               std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
    };
    SkinMesh candidate;
    candidate.vertices = word();
    const auto count = word();
    candidate.frames = word();
    candidate.logicalSize = word();
    if (candidate.vertices < 3 || candidate.vertices > 8192 || count < 3 ||
        count > 49152 || count % 3 || (candidate.frames != 1 && candidate.frames != SkinClipFrames) ||
        candidate.logicalSize == 0 || candidate.logicalSize > 128)
        return fail("invalid skin mesh dimensions");
    const std::uint64_t floats = candidate.vertices * (2ULL + 6ULL * candidate.frames);
    if (20ULL + floats * 4 + count * 4ULL != static_cast<std::uint64_t>(length))
        return fail("skin mesh payload length mismatch");
    candidate.uv.resize(candidate.vertices * 2);
    candidate.indices.resize(count);
    candidate.poses.resize(candidate.vertices * 6 * candidate.frames);
    auto scalar = [&]() { return std::bit_cast<float>(word()); };
    for (float &v : candidate.uv)
    {
        v = scalar();
        if (!std::isfinite(v) || v < 0 || v > 1) return fail("invalid skin UV");
    }
    for (auto &index : candidate.indices)
    {
        index = word();
        if (index >= candidate.vertices) return fail("skin index out of bounds");
    }
    for (std::size_t i = 0; i < candidate.poses.size(); ++i)
    {
        float &v = candidate.poses[i];
        v = scalar();
        if (!std::isfinite(v) || std::abs(v) > (i % 6 < 2 ? 4.f : 1.001f))
            return fail("invalid skin vertex");
    }
    if (!complete) return fail("truncated skin mesh");
    candidate.identity = nextIdentity.fetch_add(1, std::memory_order_relaxed);
    *this = std::move(candidate);
    return true;
}
bool SkinMesh::loadDetailUV(StreamBackend &input, std::string &error)
{
    auto fail = [&](const char *why) { error = why; return false; };
    if (!vertices || vertices > 8192 || !input.isValid()) return fail("invalid detail UV mesh");
    input.seekFromEnd(0);
    if (input.getPosition() != 8 + vertices * 8) return fail("detail UV payload length mismatch");
    input.seekFromStart(0);
    std::array<char, 4> magic{};
    if (!input.readExact(magic.data(), magic.size()) || magic != std::array<char, 4>{'G','U','V','1'})
        return fail("unsupported detail UV format");
    auto word = [&]() {
        std::array<unsigned char, 4> b{};
        if (!input.readExact(b.data(), b.size())) return std::uint32_t(0xffffffff);
        return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
    };
    if (word() != vertices) return fail("detail UV vertex count mismatch");
    std::vector<float> candidate(vertices * 2);
    for (auto &v : candidate) {
        v = std::bit_cast<float>(word());
        if (!std::isfinite(v) || v < 0 || v > 1) return fail("invalid detail UV coordinate");
    }
    detailUV = std::move(candidate);
    identity = nextIdentity.fetch_add(1, std::memory_order_relaxed);
    error.clear();
    return true;
}
SkinMesh SkinMesh::rotatedView(unsigned angle, const std::array<float, 16> &inverse,
                              const std::array<float, 16> &projection,
                              const std::array<float, 9> &normals) const
{
    if (!identity || frames != 1 || angle >= 360) return {};
    SkinMesh result = *this;
    result.identity = nextIdentity.fetch_add(1, std::memory_order_relaxed);
    const double radians = -static_cast<double>(angle) * 3.14159265358979323846 / 180;
    const double cosine = std::cos(radians), sine = std::sin(radians);
    auto point = [](const std::array<float, 16> &matrix, double x, double y, double z) {
        return std::array<double, 3>{
            matrix[0] * x + matrix[1] * y + matrix[2] * z + matrix[3],
            matrix[4] * x + matrix[5] * y + matrix[6] * z + matrix[7],
            matrix[8] * x + matrix[9] * y + matrix[10] * z + matrix[11],
        };
    };
    for (unsigned vertex = 0; vertex < vertices; ++vertex)
    {
        const auto offset = vertex * 6;
        const auto model = point(inverse, poses[offset], poses[offset + 1], poses[offset + 2]);
        // Exported swarm pivots are the world origin. Rotate about world Z,
        // preserving the standardized camera height, scale and ground alignment.
        const auto projected = point(projection,
            cosine * model[0] - sine * model[1], sine * model[0] + cosine * model[1], model[2]);
        std::array<double, 3> normal{};
        for (unsigned axis = 0; axis < 3; ++axis)
            normal[axis] = normals[axis * 3] * poses[offset + 3] +
                           normals[axis * 3 + 1] * poses[offset + 4] +
                           normals[axis * 3 + 2] * poses[offset + 5];
        const double nx = cosine * normal[0] - sine * normal[1];
        const double ny = sine * normal[0] + cosine * normal[1];
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            result.poses[offset + axis] = static_cast<float>(projected[axis]);
            // The normal rotation is orthonormal; its transpose returns to camera space.
            result.poses[offset + 3 + axis] = static_cast<float>(
                normals[axis] * nx + normals[axis + 3] * ny + normals[axis + 6] * normal[2]);
        }
    }
    return result;
}

}
