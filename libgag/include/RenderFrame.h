// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <RenderResources.h>
#include <functional>
#include <optional>
#include <span>
#include <array>

namespace GAGCore
{
namespace detail { SDL_Rect intersectTargetScissor(SDL_Rect, const std::optional<SDL_Rect> &); }
struct RenderTransform
{
    float scale = 1, x = 0, y = 0;
    std::optional<SDL_Rect> bounds;
};
struct WorldVertex
{
    float position[4], color[3], uv[2], normal[3];
};
static_assert(sizeof(WorldVertex) == 48);
static_assert(offsetof(WorldVertex, position) == 0 && offsetof(WorldVertex, color) == 16 &&
    offsetof(WorldVertex, uv) == 28 && offsetof(WorldVertex, normal) == 36);
struct WorldMaterial
{
    // Column-major; X/Y [-1,+1], depth [0,1]. Backend shader handles API Z.
    std::array<float, 16> projection{};
    std::array<float, 4> uvTransform{1, 1, 0, 0};
    std::array<float, 4> sunSpecular{0, 0, 1, 0};
    bool operator==(const WorldMaterial &) const = default;
};
struct SkinPose
{
    std::array<float,512> bones{};
    std::array<float,16> view{};
    std::array<float,9> normalView{};
};
struct SkinMaterial
{
    float shell = 0, furLength = 0, shellDepth = 0;
    std::array<float,2> region{0,0};
    std::shared_ptr<const SkinPose> pose;
};
struct SeamConstants
{
    std::int32_t height=0, cast=0, castWidth=0, fringe=0, fringeWidth=0;
    std::array<std::uint8_t,3> fringeColor{};
};
// Mirrors TerrainVisual::Surf; period zero disables surf for the slot.
struct SurfConstants
{
    std::int32_t period=0, reach=0, foam=0, foamWidth=0, shallow=0, shallowWidth=0, wet=0, wetWidth=0;
    std::array<std::uint8_t,3> shallowColor{}, foamColor{};
    std::int32_t ticks=1;
};
struct CoverageSample
{
    std::array<std::uint16_t,3> others{};
    std::uint16_t margin=0;
    std::uint8_t slots=0;
};
struct CoverageData
{
    std::uint64_t identity=0, revision=0;
    std::uint32_t width=0,height=0;
    std::vector<CoverageSample> samples;
};
// Composition preserves the CPU straight-RGBA integer reference; the target
// descriptor selects the final straight or premultiplied representation.
struct WeightedComposeData
{
    std::array<TextureSnapshot,4> materials;
    std::array<SeamConstants,4> seams;
    std::array<SurfConstants,4> surfs;
    // Q8 world pixels of the cell origin and the surf animation time.
    std::int32_t surfOriginX=0, surfOriginY=0;
    std::uint32_t surfTime=0;
    std::uint32_t paletteCount=0,compositionSize=32,reductionDivisor=1;
    std::shared_ptr<const CoverageData> coverage;
    SDL_Rect targetRect{}; // Target pixels, top-left origin.
};
// Surf uniforms in manifest order and std140 layout: surfPlace, then
// surfShape[4], surfBand[4] and surfColor[4], one ivec4 per palette slot.
inline std::array<std::int32_t,52> surfUniforms(const WeightedComposeData &data)
{
    std::array<std::int32_t,52> values{data.surfOriginX,data.surfOriginY,std::int32_t(data.surfTime),0};
    for(unsigned slot=0;slot<4;++slot) {
        const auto &surf=data.surfs[slot];
        const auto pack=[](const std::array<std::uint8_t,3> &c){return std::int32_t(c[0])<<16|std::int32_t(c[1])<<8|c[2];};
        const std::int32_t shape[]{surf.period,surf.reach,surf.foam,surf.foamWidth},
            band[]{surf.shallow,surf.shallowWidth,surf.wet,surf.wetWidth},color[]{pack(surf.shallowColor),pack(surf.foamColor),surf.ticks,0};
        for(unsigned k=0;k<4;++k) {
            values[4+slot*4+k]=shape[k];values[20+slot*4+k]=band[k];values[36+slot*4+k]=color[k];
        }
    }
    return values;
}
struct DrawPacket
{
    enum class Kind { Fill, Blit, Triangles, ScreenTriangles, Barrier, Clear, MeshTriangles, WeightedCompose };
    Kind kind = Kind::Fill;
    enum class Material { Primitive, WorldLit, Cloud, SmoothPoint, Skin, SkinRig };
    enum class Blend { Automatic, SourceOver, Premultiplied, Additive, Opaque };
    enum class Depth { Disabled, Less, LessEqual };
    enum class Cull { Disabled, Back };
    Material material = Material::Primitive;
    Blend blend = Blend::Automatic;
    Depth depth = Depth::Disabled;
    Cull cull = Cull::Disabled;
    bool depthWrite = false;
    WorldMaterial world;
    std::shared_ptr<const SkinMaterial> skin;
    std::shared_ptr<const WeightedComposeData> weightedCompose;
    GeometrySnapshot geometry;
    std::uint32_t first = 0, count = 0;
    bool indexed = false;
    float targetLogicalWidth = 0, targetLogicalHeight = 0;
    std::optional<SDL_Rect> viewport; // Target pixels, top-left origin.
    bool clearColor = false, clearDepth = false;
    SDL_FColor clearValue{0, 0, 0, 1};
    float clearDepthValue = 1;
    TextureSnapshot texture, secondTexture;
    TextureSnapshot target; // Empty means the device's presentation target.
    RenderTransform transform;
    std::optional<SDL_Rect> clip;
    // Exact backing-pixel bounds, independent of logical scaling. Intersects
    // ordinary clipping; coordinates have a top-left origin in this target.
    std::optional<SDL_Rect> targetScissor;
    SDL_Rect source{};
    SDL_FRect destination{};
    SDL_Color color{255, 255, 255, 255};
    std::vector<SDL_Vertex> vertices;
    bool opaque = false, linear = false;
    Uint8 alpha = 255;
    bool compatible(const DrawPacket &) const;
};
struct RenderFrame
{
    static constexpr std::size_t CommandCapacity = 4096;
    static constexpr std::size_t VertexCapacity = 65536;
    std::uint64_t identity = 0;
    int logicalWidth = 0, logicalHeight = 0;
    std::vector<DrawPacket> packets;
    std::size_t vertexCount = 0;
    // Returns only adjacent compatible spans; never changes painter order.
    std::size_t batchEnd(std::size_t begin) const;
};
class RenderFrameRecorder
{
    RenderFrame frame;
    std::function<void(RenderFrame)> submit;
  public:
    explicit RenderFrameRecorder(std::function<void(RenderFrame)> submit);
    RenderFrameRecorder(const RenderFrameRecorder &) = delete;
    RenderFrameRecorder &operator=(const RenderFrameRecorder &) = delete;
    void begin(std::uint64_t identity, int logicalWidth, int logicalHeight);
    void append(DrawPacket);
    void flush();
    void cancel() noexcept;
    std::size_t size() const { return frame.packets.size(); }
};
} // namespace GAGCore
