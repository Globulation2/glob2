// SPDX-License-Identifier: GPL-3.0-or-later
#include <SkinMesh.h>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <StreamBackend.h>
#include <limits>
#include <utility>

namespace GAGCore
{
bool SkinMesh::load(const std::string &path, std::string &error)
{
    FileStreamBackend input(std::fopen(path.c_str(), "rb"));
    return load(input, error);
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
        count > 49152 || count % 3 || (candidate.frames != 1 && candidate.frames != 256) ||
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
    static std::atomic<std::uint64_t> nextIdentity{1};
    candidate.identity = nextIdentity.fetch_add(1, std::memory_order_relaxed);
    *this = std::move(candidate);
    return true;
}
}
