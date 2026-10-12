// SPDX-License-Identifier: GPL-3.0-or-later
#include <RenderFrame.h>
#include <stdexcept>
#include <algorithm>
#include <cstdint>

namespace GAGCore
{
namespace detail
{
SDL_Rect intersectTargetScissor(SDL_Rect bounds, const std::optional<SDL_Rect> &scissor)
{
    if (!scissor) return bounds;
    if (scissor->w < 0 || scissor->h < 0) throw std::invalid_argument("Negative target scissor extent");
    // Widen endpoint sums before intersection, including off-target rectangles.
    const auto left = std::max<std::int64_t>(bounds.x, scissor->x);
    const auto top = std::max<std::int64_t>(bounds.y, scissor->y);
    const auto right = std::min<std::int64_t>(std::int64_t(bounds.x) + bounds.w, std::int64_t(scissor->x) + scissor->w);
    const auto bottom = std::min<std::int64_t>(std::int64_t(bounds.y) + bounds.h, std::int64_t(scissor->y) + scissor->h);
    if (right <= left || bottom <= top) return {};
    return {int(left), int(top), int(right-left), int(bottom-top)};
}
}
namespace
{
bool equal(const std::optional<SDL_Rect> &a, const std::optional<SDL_Rect> &b)
{
    return a.has_value() == b.has_value() && (!a || (a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h));
}
}
bool DrawPacket::compatible(const DrawPacket &other) const
{
    if (linear && kind == Kind::Blit && (source.x != other.source.x || source.y != other.source.y ||
        source.w != other.source.w || source.h != other.source.h)) return false;
    return kind != Kind::Barrier && kind != Kind::WeightedCompose && kind != Kind::Clear && kind != Kind::MeshTriangles && kind == other.kind &&
        material == other.material && skin == other.skin && secondTexture == other.secondTexture && blend == other.blend && depth == other.depth && depthWrite == other.depthWrite && cull == other.cull &&
        equal(viewport, other.viewport) && targetLogicalWidth == other.targetLogicalWidth && targetLogicalHeight == other.targetLogicalHeight && texture == other.texture && target == other.target &&
        opaque == other.opaque && linear == other.linear && alpha == other.alpha &&
        transform.scale == other.transform.scale && transform.x == other.transform.x && transform.y == other.transform.y &&
        equal(transform.bounds, other.transform.bounds) && equal(clip, other.clip) && equal(targetScissor, other.targetScissor);
}
std::size_t RenderFrame::batchEnd(std::size_t begin) const
{
    if (begin >= packets.size()) return packets.size();
    std::size_t end = begin + 1;
    while (end < packets.size() && packets[begin].compatible(packets[end])) ++end;
    return end;
}
RenderFrameRecorder::RenderFrameRecorder(std::function<void(RenderFrame)> callback) : submit(std::move(callback))
{
    if (!submit) throw std::invalid_argument("Missing frame submission callback");
}
void RenderFrameRecorder::begin(std::uint64_t identity, int width, int height)
{
    flush();
    if (width <= 0 || height <= 0) throw std::invalid_argument("Invalid frame logical size");
    frame.identity = identity; frame.logicalWidth = width; frame.logicalHeight = height;
}
void RenderFrameRecorder::append(DrawPacket packet)
{
    if (packet.vertices.size() > RenderFrame::VertexCapacity) throw std::length_error("Draw packet exceeds vertex capacity");
    if (frame.packets.size() == RenderFrame::CommandCapacity || packet.vertices.size() > RenderFrame::VertexCapacity - frame.vertexCount)
        flush();
    frame.vertexCount += packet.vertices.size();
    frame.packets.push_back(std::move(packet));
}
void RenderFrameRecorder::flush()
{
    if (frame.packets.empty()) return;
    RenderFrame outgoing;
    outgoing.identity = frame.identity; outgoing.logicalWidth = frame.logicalWidth; outgoing.logicalHeight = frame.logicalHeight;
    outgoing.packets.swap(frame.packets); outgoing.vertexCount = frame.vertexCount;
    frame.vertexCount = 0;
    submit(std::move(outgoing));
}
void RenderFrameRecorder::cancel() noexcept { frame.packets.clear(); frame.vertexCount = 0; }
} // namespace GAGCore
