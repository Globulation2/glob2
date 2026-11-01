// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
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
// Borrowed tightly packed CPU-authored mip data, including level zero.
struct RenderMipLevel
{
    int width = 0, height = 0;
    SDL_PixelFormat format = SDL_PIXELFORMAT_RGBA32;
    std::span<const std::byte> pixels;
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
};
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
    explicit RenderResources(std::size_t byteLimit = 256 * 1024 * 1024, std::size_t slotLimit = 16384);
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
