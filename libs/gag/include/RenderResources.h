// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>
#include <span>
#include <cstddef>

namespace GAGCore
{
struct TextureHandle
{
    std::uint32_t slot = 0, generation = 0;
    explicit operator bool() const { return generation != 0; }
    bool operator==(const TextureHandle &) const = default;
};
struct BufferHandle
{
    std::uint32_t slot = 0, generation = 0;
    explicit operator bool() const { return generation != 0; }
    bool operator==(const BufferHandle &) const = default;
};
// GPU block formats a device may sample directly. Both store 16 bytes for
// each 4x4 texels; blocks hold premultiplied colour.
enum class TextureCompression : unsigned char { Uncompressed, BC3, ASTC4x4 };
//! Bytes of one level's 4x4 blocks.
constexpr std::size_t compressedLevelBytes(int width, int height)
{
    return std::size_t((width + 3) / 4) * std::size_t((height + 3) / 4) * 16;
}
// Borrowed tightly packed CPU-authored mip data, including level zero.
struct RenderMipLevel
{
    int width = 0, height = 0;
    SDL_PixelFormat format = SDL_PIXELFORMAT_RGBA32;
    std::span<const std::byte> pixels;
    // Optional: the level's premultiplied 4x4 blocks in `compression`. A chain
    // whose leading levels all carry blocks (each level at least 4x4) may be
    // stored compressed; pixels may then be empty for those levels.
    TextureCompression compression = TextureCompression::Uncompressed;
    std::span<const std::byte> blocks;
};
struct OwnedRenderMipLevel
{
    int width = 0, height = 0;
    std::vector<std::byte> rgba;
};
struct TextureDescriptor
{
    enum class Sampling { Nearest, Linear };
    // Premultiplied describes owned pixels too; upload never multiplies them twice.
    enum class Alpha { Straight, Opaque, Premultiplied };
    // Generated denotes an authored complete CPU chain, never driver generation.
    enum class Mips { Disabled, Generated };
    int width = 0, height = 0;
    SDL_PixelFormat format = SDL_PIXELFORMAT_RGBA32;
    Sampling sampling = Sampling::Nearest;
    Alpha alpha = Alpha::Straight;
    Mips mips = Mips::Disabled;
    enum class Address { Clamp, Repeat };
    Address addressU = Address::Clamp, addressV = Address::Clamp;
    bool depthAttachment = false;
    enum class BudgetClass { General, SkinAtlas, TerrainPage, TerrainMask };
    BudgetClass budgetClass = BudgetClass::General;
    // Not Uncompressed: the revision holds blocks instead of pixels (always
    // Alpha::Premultiplied, Mips::Generated, never a render target).
    TextureCompression compression = TextureCompression::Uncompressed;
};
// Memory a presentation device and its CPU restoration registry may hold.
// Class limits are maxima, not a partition of the total: they need not all fit
// at once. A class limit is also the largest single texture of that class.
struct RenderBudget
{
    std::size_t total = 0;                 // Textures, depth and transient device work together.
    std::array<std::size_t, 4> classes{};  // By TextureDescriptor::BudgetClass.
    std::size_t geometry = 0;              // Persistent device geometry.
    std::size_t classLimit(TextureDescriptor::BudgetClass category) const
    { return classes.at(std::size_t(category)); }
    bool operator==(const RenderBudget &) const = default;
    static RenderBudget desktop();
    static RenderBudget mobile();
};
// The budget of this device: mobile on phones and tablets (the Android and
// iOS builds, and the browser build on them; GAGCore::platformIsMobile),
// desktop elsewhere. GLOB2_RENDER_BUDGET=mobile or desktop overrides it (measurement).
const RenderBudget &platformRenderBudget();
enum class GeometryLayout { Primitive32, World48, Skin40, SkinRig72 };
struct GeometryRevision
{
    BufferHandle handle;
    std::uint64_t contentRevision = 0;
    GeometryLayout layout = GeometryLayout::World48;
    std::uint32_t stride = 48;
    std::vector<std::byte> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t bytes = 0;
};
using GeometrySnapshot = std::shared_ptr<const GeometryRevision>;
// A revision is immutable after publication. A queued/submitted packet retains
// its own revision even when the logical texture is changed or deleted.
struct TextureRevision
{
    TextureHandle handle;
    TextureDescriptor descriptor;
    std::uint64_t contentRevision = 0;
    std::shared_ptr<SDL_Surface> pixels;
    std::size_t bytes = 0;
    // Levels one and higher; pixels owns the padded RGBA32 level zero.
    std::vector<OwnedRenderMipLevel> mips;
    // Compressed revisions only, with null pixels and no mips: each level's
    // blocks, level zero first, ending before the first level under 4x4.
    std::vector<std::vector<std::byte>> blocks;
};
using TextureSnapshot = std::shared_ptr<const TextureRevision>;

class RenderResources
{
    struct Budget;
    struct Slot { std::uint32_t generation = 1; TextureSnapshot current; };
    struct BufferSlot { std::uint32_t generation = 1; GeometrySnapshot current; };
    std::shared_ptr<Budget> budget, geometryBudget;
    std::vector<BufferSlot> buffers;
    std::vector<Slot> slots;
    std::size_t maximumSlots;
    const std::uint64_t identity;
    TextureSnapshot copy(TextureHandle, SDL_Surface *, std::uint64_t, TextureDescriptor, std::span<const RenderMipLevel>);
  public:
    explicit RenderResources(const RenderBudget & = platformRenderBudget(), std::size_t slotLimit = 16384);
    // A total limit with the platform's class limits, each capped by it (tests).
    explicit RenderResources(std::size_t byteLimit, std::size_t slotLimit = 16384);
    RenderResources(const RenderResources &) = delete;
    RenderResources &operator=(const RenderResources &) = delete;
    // Admission failure returns an empty handle/snapshot; it never evicts pixels
    // still referenced by an older submission. Callers may flush/defer and retry.
    TextureHandle create(SDL_Surface *, std::uint64_t revision, TextureDescriptor = {}, std::span<const RenderMipLevel> mipLevels = {});
    bool update(TextureHandle, SDL_Surface *, std::uint64_t revision, TextureDescriptor = {}, std::span<const RenderMipLevel> mipLevels = {});
    TextureSnapshot snapshot(TextureHandle) const;
    bool erase(TextureHandle);
    std::size_t retainedBytes() const;
    std::size_t byteLimit() const;
    //! The most one budget class may retain, within byteLimit().
    std::size_t classLimit(TextureDescriptor::BudgetClass) const;
    std::uint64_t instanceIdentity() const { return identity; }
    BufferHandle createGeometry(GeometryLayout, std::span<const std::byte> vertices,
                                std::span<const std::uint32_t> indices, std::uint64_t revision);
    GeometrySnapshot snapshot(BufferHandle) const;
    bool erase(BufferHandle);
    std::size_t retainedGeometryBytes() const;
};

struct UploadRequest
{
    TextureSnapshot texture;
    std::uint64_t deviceGeneration = 0;
};
// Device-local bounded admission. Logical revisions survive cancellation/reset;
// uploaded revisions must be tracked by each backend independently.
class RenderUploadQueue
{
    std::deque<UploadRequest> requests;
    std::size_t bytes = 0, byteLimit, requestLimit;
  public:
    explicit RenderUploadQueue(std::size_t byteLimit = 8 * 1024 * 1024, std::size_t requestLimit = 64);
    bool enqueue(UploadRequest);
    bool pop(UploadRequest &);
    void cancel();
    std::size_t size() const { return requests.size(); }
    std::size_t pendingBytes() const { return bytes; }
};
} // namespace GAGCore
