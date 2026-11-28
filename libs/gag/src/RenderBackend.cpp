// SPDX-License-Identifier: GPL-3.0-or-later
#include <RenderBackend.h>
#include <algorithm>
#include <limits>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <optional>
#include <vector>
#include <stdexcept>
#include <unordered_map>

namespace GAGCore
{
namespace
{
bool update(SDL_Texture *texture, SDL_Surface *pixels)
{
	if (!SDL_LockSurface(pixels))
		return false;
	const bool result = SDL_UpdateTexture(texture, nullptr, pixels->pixels, pixels->pitch);
	SDL_UnlockSurface(pixels);
	return result;
}
// Exactly (channel * alpha + 127) / 255 per channel, red and blue sharing one
// multiply; opaque and transparent pixels, most of the artwork, skip it. With
// t = channel * alpha + 128, (t + (t >> 8)) >> 8 is that rounded division for
// every product up to 255 * 255, so whole and region uploads (and the GPU
// blend that exactPremultiplication accepts) all store the same bytes.
void premultiplyRow(Uint32 *row, int width)
{
	for (int x = 0; x < width; ++x)
	{
		const Uint32 p = row[x], a = p >> 24;
		if (a == 255)
			continue;
		if (a == 0)
		{
			row[x] = 0;
			continue;
		}
		Uint32 rb = (p & 0xff00ff) * a + 0x800080, g = ((p >> 8) & 255) * a + 128;
		rb = ((rb + ((rb >> 8) & 0xff00ff)) >> 8) & 0xff00ff;
		g = ((g + (g >> 8)) >> 8) & 255;
		row[x] = (a << 24) | rb | (g << 8);
	}
}
void check(bool result)
{
	if (!result)
		throw std::runtime_error(SDL_GetError());
}
bool gpuPreparationEnabled(SDL_Renderer *renderer)
{
	const char *disabled = std::getenv("GLOB2_DISABLE_GPU_TEXTURE_PREPARATION");
	return std::strcmp(SDL_GetRendererName(renderer), "software") != 0 && (!disabled || !*disabled);
}
class SDLRenderBackend final : public RenderBackend
{
	SDL_Renderer *renderer;
	float scale = 1, offsetX = 0, offsetY = 0;
    float nativeX = 1, nativeY = 1;
    std::optional<SDL_Rect> physicalClip;
	std::optional<SDL_Rect> bounds;
	struct Texture
	{
		SDL_Texture *handle;
		std::uint64_t revision;
		bool linear, gpu;
		int width, height;
		SDL_PixelFormat format;
		// Accounting only: never dereferenced.
		const SDL_Surface *source = nullptr;
		std::uint64_t bytes = 0, sourceBytes = 0;
		// Modes last applied to the SDL texture; additive draws borrow it.
		SDL_BlendMode blend = SDL_BLENDMODE_BLEND;
		SDL_ScaleMode scaleMode = SDL_SCALEMODE_NEAREST;
		// Sequence of the newest pending run drawing this texture, if any.
		std::uint32_t newestRun = 0;
	};
	// Geometry batching. Each SDL_RenderGeometry call costs a driver draw (and,
	// on GLES, state validation), and a frame interleaves sprites from dozens
	// of textures, so plain consecutive merging finds few neighbours. Instead,
	// submissions sharing a texture, its blend and scale modes and SDL's
	// per-call wrap choice accumulate in up to MaximumRuns pending runs, and a
	// draw may join an earlier compatible run, moving ahead of later runs,
	// only when none of those later runs touches any grid cell its output
	// bounds cover. Overlapping draws therefore keep painter order whatever
	// their blending, and the output is the pixels separate calls would give.
	// Every state change that SDL would capture at queue time (clip, render
	// scale, target, texture contents or destruction) submits all pending runs
	// first; so must any texture upload or render-target switch.
	struct BatchState
	{
		Texture *texture = nullptr;
		SDL_BlendMode blend = SDL_BLENDMODE_BLEND;
		SDL_ScaleMode scaleMode = SDL_SCALEMODE_NEAREST;
		bool wrapU = false, wrapV = false;
		bool operator==(const BatchState &) const = default;
	};
	struct Bounds
	{
		float left, top, right, bottom;
	};
	struct Run
	{
		BatchState state;
		std::uint32_t sequence = 0;
		std::vector<SDL_Vertex> vertices;
		std::vector<int> indices;
	};
	// Each cell holds the sequence of the newest run drawn over it. Sequences
	// below the oldest pending run's belong to submitted runs and constrain
	// nothing, so the grid never needs clearing. Coordinates beyond the grid
	// clamp to its edge cells, which stays conservative. Sixteen pixels is
	// about a small sprite: finer cells cost more marking per draw, coarser
	// ones see false overlaps between neighbouring sprites. 256 cells cover a
	// 4096-pixel output.
	static constexpr int GridCell = 16, GridColumns = 256, GridRows = 256;
	std::vector<std::uint32_t> occupancy = std::vector<std::uint32_t>(GridColumns * GridRows, 0);
	std::uint32_t nextSequence = 1, untexturedRun = 0, opaqueFillRun = 0;
	mutable int maximumTexture = 0;
	// MaximumRuns bounds the pending state (a full set submits its older
	// half); DirectSubmissionVertices is where a single list is large enough
	// that merging saves nothing and copying it would cost.
	static constexpr std::size_t MaximumBatchVertices = 16384, MaximumRuns = 64, RetainedRunVertices = 2048,
								 DirectSubmissionVertices = 384;
	std::vector<SDL_Vertex> direct;
	// runs[0, runCount) are pending in submission order; later slots keep their
	// storage for reuse.
	std::vector<Run> runs;
	std::size_t runCount = 0;
	// Accelerated renderers rasterize shared edges once, so touching draws may
	// reorder; SDL's software renderer rounds rectangle edges and keeps at least
	// one output pixel (in logical units) between them.
	float boundsMargin = 0;
	// SDL's software renderer turns triangle pairs into rectangle copies; explicit
	// indices reproduce its per-call duplicate-vertex detection so a merged batch
	// pairs exactly the triangles separate calls would have.
	bool indexedBatches = false;
	// SDL's software renderer RLE-encodes static textures on their first blit,
	// and SDL 3.4's clipped scaled blit then loses an encoded texture's pixel
	// pointer (it saves the null pointer of the encoded surface around a scale
	// that decodes it). A later SDL_UpdateTexture writes through that null
	// pointer. So on this renderer a texture is updated in place only when it
	// is streaming, which SDL never encodes; see changeableTexture().
	bool softwareRenderer = false;
	std::optional<SDL_Rect> appliedClip;
	bool clipKnown = false;
	std::unordered_map<const void *, Texture> textures;
	// Straight-alpha staging for GPU premultiplication, one per texture size.
	// Linear textures of several sizes upload in one frame (pages, HD sprite
	// frames, overlay tiles): a single staging texture recreated whenever the
	// size changed made each such upload allocate a texture.
	struct Staging { SDL_Texture *texture; int w, h; std::uint64_t used, presented; };
	std::vector<Staging> staging;
	std::uint64_t stagingUse = 0, presents = 0;
	static constexpr std::size_t MaxStaging = 4;
	SDL_Texture *uploadTexture = nullptr; // The staging texture of the current upload.
	SDL_Texture *stagingFor(int w, int h)
	{
		for (auto &entry : staging)
			if (entry.w == w && entry.h == h)
			{
				entry.used = ++stagingUse;
				entry.presented = presents;
				return entry.texture;
			}
		if (staging.size() >= MaxStaging)
		{
			auto oldest = std::min_element(staging.begin(), staging.end(),
				[](const Staging &a, const Staging &b) { return a.used < b.used; });
			// Draws queued with the evicted texture go first.
			submitBatch();
			SDL_DestroyTexture(oldest->texture);
			staging.erase(oldest);
		}
		auto *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
		if (!texture || !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) ||
			!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST))
		{
			SDL_DestroyTexture(texture);
			return nullptr;
		}
		staging.push_back({texture, w, h, ++stagingUse, presents});
		return texture;
	}
	void releaseStaging()
	{
		for (auto &entry : staging)
			SDL_DestroyTexture(entry.texture);
		staging.clear();
		uploadTexture = nullptr;
	}
	bool gpuPreparation;
	const SDL_Texture *lastTexture = nullptr;
	std::uint64_t textureBytes = 0, peakTextureBytes = 0;
	static std::uint64_t storageBytes(SDL_Texture *texture)
	{
		if (!texture) return 0;
		const auto format = SDL_PixelFormat(SDL_GetNumberProperty(
			SDL_GetTextureProperties(texture), SDL_PROP_TEXTURE_FORMAT_NUMBER, 0));
		const int bytes = SDL_BYTESPERPIXEL(format);
		return std::uint64_t(texture->w) * texture->h * (bytes ? bytes : 4);
	}
	void erase(std::unordered_map<const void *, Texture>::iterator found)
	{
		submitBatch();
		textureBytes -= found->second.bytes;
		++counts.textureDestroys;
		textures.erase(found);
	}

	// A single straight-alpha staging texture is reused; retained linear
	// textures contain premultiplied pixels. No CPU copy or pixel loop is needed.
	bool premultiply(SDL_Texture *target, SDL_Surface *pixels)
	{
		uploadTexture = stagingFor(pixels->w, pixels->h);
		if (!uploadTexture)
			return false;
		if (!update(uploadTexture, pixels))
			return false;
		++counts.textureUploads;
		++counts.gpuPreparedUploads;
		counts.textureUploadBytes += std::uint64_t(pixels->w) * pixels->h * 4;
		lastTexture = nullptr; // A target switch breaks SDL's draw batch.
		auto *previous = SDL_GetRenderTarget(renderer);
		SDL_FColor color;
		if (!SDL_GetRenderDrawColorFloat(renderer, &color.r, &color.g, &color.b, &color.a))
			return false;
		if (!SDL_SetRenderTarget(renderer, target))
			return false;
		// Viewport, clip, scale and logical presentation belong to each target
		// in SDL. Switching back restores the caller's state, including clips.
		const bool result = SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0) &&
							SDL_RenderClear(renderer) &&
							SDL_RenderTexture(renderer, uploadTexture, nullptr, nullptr);
		const bool restored = SDL_SetRenderTarget(renderer, previous);
		const bool colorRestored =
			SDL_SetRenderDrawColorFloat(renderer, color.r, color.g, color.b, color.a);
		check(restored && colorRestored);
		return result;
	}
    bool exactPremultiplication()
    {
        // Blend units can approximate src*alpha differently from our integer
        // artwork preparation. Test every byte pair once, before caching any
        // gameplay texture, rather than changing the established pixel result.
        using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
        using TextureOwner = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>;
        Surface source(SDL_CreateSurface(256, 256, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
        TextureOwner target(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                              SDL_TEXTUREACCESS_TARGET, 256, 256), SDL_DestroyTexture);
        if (!source || !target) return false;
        for (unsigned alpha = 0; alpha < 256; ++alpha)
            for (unsigned channel = 0; channel < 256; ++channel)
                reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(source->pixels) + alpha * source->pitch)[channel] =
                    (alpha << 24) | (channel << 16) | ((255 - channel) << 8) | channel;
        if (!premultiply(target.get(), source.get())) return false;
        auto *previous = SDL_GetRenderTarget(renderer);
        if (!SDL_SetRenderTarget(renderer, target.get())) return false;
        Surface output(SDL_RenderReadPixels(renderer, nullptr), SDL_DestroySurface);
        check(SDL_SetRenderTarget(renderer, previous));
        if (!output || output->w != 256 || output->h != 256 || SDL_BYTESPERPIXEL(output->format) != 4) return false;
        unsigned mismatches = 0;
        const auto *format = SDL_GetPixelFormatDetails(output->format);
        for (unsigned alpha = 0; alpha < 256; ++alpha)
            for (unsigned channel = 0; channel < 256; ++channel) {
                Uint32 value = 0;
                std::memcpy(&value, static_cast<Uint8 *>(output->pixels) + alpha * output->pitch +
                            channel * SDL_BYTESPERPIXEL(output->format), SDL_BYTESPERPIXEL(output->format));
                Uint8 red, green, blue, a;
                SDL_GetRGBA(value, format, nullptr, &red, &green, &blue, &a);
                const unsigned expected = (channel * alpha + 127) / 255;
                mismatches += red != expected || blue != expected ||
                    green != ((255 - channel) * alpha + 127) / 255 || a != alpha;
            }
        if (mismatches)
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER,
                        "Renderer %s uses CPU texture preparation: GPU byte conformance failed for %u/65536 pairs",
                        SDL_GetRendererName(renderer), mismatches);
        return mismatches == 0;
    }
	RenderOperations counts;

	SDL_Texture *applyModes(const BatchState &state)
	{
		auto *entry = state.texture;
		if (!entry)
			return nullptr;
		if (entry->blend != state.blend)
		{
			check(SDL_SetTextureBlendMode(entry->handle, state.blend));
			entry->blend = state.blend;
		}
		if (entry->scaleMode != state.scaleMode)
		{
			check(SDL_SetTextureScaleMode(entry->handle, state.scaleMode));
			entry->scaleMode = state.scaleMode;
		}
		return entry->handle;
	}
	// Opaque fill runs (untextured, SDL_BLENDMODE_NONE) hold whole quads in
	// fill()'s vertex order. Replacing a source of alpha 1 is what blending it
	// computes, without reading the target: SDL's software renderer fills
	// rectangles directly instead of blending each pixel, and tiled GPUs skip
	// the destination read.
	bool submitOpaqueFills(std::span<const SDL_Vertex> vertices)
	{
		bool result = SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
		if (!indexedBatches)
			result = result && SDL_RenderGeometry(renderer, nullptr, vertices.data(),
												  int(vertices.size()), nullptr, 0);
		else
		{
			// SDL_SW_RenderGeometryRaw turns each quad into this exact rectangle
			// fill, only with blending forced on.
			SDL_FColor previous;
			result = result && SDL_GetRenderDrawColorFloat(renderer, &previous.r, &previous.g,
														   &previous.b, &previous.a);
			for (std::size_t i = 0; result && i + 6 <= vertices.size(); i += 6)
			{
				const SDL_Vertex &topLeft = vertices[i], &bottomRight = vertices[i + 2];
				const SDL_FRect rect{topLeft.position.x, topLeft.position.y,
									 bottomRight.position.x - topLeft.position.x,
									 bottomRight.position.y - topLeft.position.y};
				result = SDL_SetRenderDrawColorFloat(renderer, topLeft.color.r, topLeft.color.g,
													 topLeft.color.b, topLeft.color.a) &&
						 SDL_RenderFillRect(renderer, &rect);
			}
			result = SDL_SetRenderDrawColorFloat(renderer, previous.r, previous.g, previous.b,
												 previous.a) && result;
		}
		return SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) && result;
	}
	// One SDL draw call in `state`. Indices are given only for indexedBatches.
	bool render(const BatchState &state, std::span<const SDL_Vertex> vertices, std::span<const int> indices)
	{
		SDL_Texture *texture = applyModes(state);
		++counts.geometryBatches;
		if (!texture && state.blend == SDL_BLENDMODE_NONE)
			return submitOpaqueFills(vertices);
		return SDL_RenderGeometry(renderer, texture, vertices.data(), int(vertices.size()),
								  indices.empty() ? nullptr : indices.data(), int(indices.size()));
	}
	void submitRun(Run &run)
	{
		const bool result = render(run.state, run.vertices, run.indices);
		run.vertices.clear();
		run.indices.clear();
		// Reused slots keep ordinary sprite runs allocation-free; an occasional
		// large triangle list does not pin its storage in every slot.
		if (run.vertices.capacity() > RetainedRunVertices)
		{
			std::vector<SDL_Vertex>().swap(run.vertices);
			std::vector<int>().swap(run.indices);
		}
		check(result);
	}
	void submitBatch()
	{
		std::size_t submitted = 0;
		try
		{
			for (; submitted < runCount; ++submitted)
				submitRun(runs[submitted]);
		}
		catch (...)
		{
			discardRuns();
			throw;
		}
		runCount = 0;
	}
	void discardRuns() noexcept
	{
		for (std::size_t i = 0; i < runCount; ++i)
		{
			runs[i].vertices.clear();
			runs[i].indices.clear();
		}
		runCount = 0;
	}
	// Recomputed whenever the mapping from queued coordinates to output pixels
	// changes (logical size, native scale, target scissor, window size).
	void refreshBoundsMargin()
	{
		if (!indexedBatches)
		{
			boundsMargin = 0;
			return;
		}
		// Output pixels per queued unit. Under a target scissor vertices are
		// already in output pixels; otherwise SDL scales them by the logical
		// presentation or the native render scale.
		float pixelScale = 1;
		int outputW = 0, outputH = 0, logicalW = 0, logicalH = 0;
		SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
		if (physicalClip)
			pixelScale = 1;
		else if (SDL_GetRenderOutputSize(renderer, &outputW, &outputH) &&
				 SDL_GetRenderLogicalPresentation(renderer, &logicalW, &logicalH, &mode) &&
				 mode != SDL_LOGICAL_PRESENTATION_DISABLED && logicalW > 0 && logicalH > 0)
			pixelScale = std::min(float(outputW) / logicalW, float(outputH) / logicalH);
		else
			pixelScale = std::min(nativeX, nativeY);
		// SDL's software renderer rounds rectangle edges, so a draw can reach
		// into the next output pixel: between draws that reorder keep one queued
		// unit plus the larger of one unit and one whole output pixel.
		boundsMargin = 1 + (pixelScale > 0 && pixelScale < 1 ? 1 / pixelScale : 1);
	}
	static bool sameVertex(const SDL_Vertex &a, const SDL_Vertex &b, bool textured)
	{
		return a.position.x == b.position.x && a.position.y == b.position.y &&
			   (!textured || (a.tex_coord.x == b.tex_coord.x && a.tex_coord.y == b.tex_coord.y)) &&
			   std::memcmp(&a.color, &b.color, sizeof(a.color)) == 0;
	}
	static int gridIndex(float value, int count)
	{
		const float cell = std::floor(value / GridCell);
		return cell < 0 ? 0 : cell >= count ? count - 1 : int(cell);
	}
	Run &runFor(const BatchState &state, const Bounds &bounds, std::size_t vertices)
	{
		if (nextSequence == std::numeric_limits<std::uint32_t>::max())
		{
			submitBatch();
			std::fill(occupancy.begin(), occupancy.end(), 0);
			for (auto &[key, texture] : textures)
				texture.newestRun = 0;
			untexturedRun = opaqueFillRun = 0;
			nextSequence = 1;
		}
		// Half-open bounds: draws that only share an edge occupy distinct cells
		// when the edge lies on a cell boundary.
		const int left = gridIndex(bounds.left, GridColumns), top = gridIndex(bounds.top, GridRows);
		const int right = gridIndex(std::nextafter(bounds.right, -std::numeric_limits<float>::infinity()), GridColumns);
		const int bottom = gridIndex(std::nextafter(bounds.bottom, -std::numeric_limits<float>::infinity()), GridRows);
		std::uint32_t latest = 0;
		for (int y = top; y <= bottom; ++y)
		{
			const auto *row = occupancy.data() + y * GridColumns;
			for (int x = left; x <= right; ++x)
				latest = std::max(latest, row[x]);
		}
		// Pending sequences are contiguous, so a texture's newest run is found
		// without scanning; a run with other modes for it starts a new run.
		Run *target = nullptr;
		std::uint32_t &newest = state.texture ? state.texture->newestRun
								: state.blend == SDL_BLENDMODE_NONE ? opaqueFillRun : untexturedRun;
		if (runCount && newest >= runs[0].sequence && newest >= latest)
		{
			Run &run = runs[newest - runs[0].sequence];
			if (run.state == state && run.vertices.size() + vertices <= MaximumBatchVertices)
				target = &run;
		}
		if (!target)
		{
			if (runCount == MaximumRuns)
			{
				// The oldest runs precede everything still pending.
				constexpr std::size_t released = MaximumRuns / 2;
				for (std::size_t i = 0; i < released; ++i)
					submitRun(runs[i]);
				std::rotate(runs.begin(), runs.begin() + released, runs.begin() + runCount);
				runCount -= released;
			}
			if (runs.size() == runCount)
				runs.emplace_back();
			target = &runs[runCount++];
			target->state = state;
			target->sequence = nextSequence++;
			newest = target->sequence;
		}
		for (int y = top; y <= bottom; ++y)
		{
			auto *row = occupancy.data() + y * GridColumns;
			for (int x = left; x <= right; ++x)
				row[x] = std::max(row[x], target->sequence);
		}
		return *target;
	}
	void queue(std::span<const SDL_Vertex> vertices, Texture *entry, SDL_BlendMode blend,
			   SDL_ScaleMode scaleMode, bool applyTransform)
	{
		if (vertices.empty())
			return;
		++counts.triangles;
		if (vertices.size() % 3 ||
			vertices.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
			throw std::invalid_argument("Invalid triangle batch size");
		SDL_Texture *texture = entry ? entry->handle : nullptr;
		counts.vertices += vertices.size();
		counts.textureSwitches += texture != lastTexture;
		lastTexture = texture;
		BatchState state;
		if (entry)
		{
			// SDL picks clamp or wrap per call from the coordinates it receives.
			state = {entry, blend, scaleMode, false, false};
			for (const auto &vertex : vertices)
			{
				state.wrapU |= vertex.tex_coord.x < 0.0f || vertex.tex_coord.x > 1.0f;
				state.wrapV |= vertex.tex_coord.y < 0.0f || vertex.tex_coord.y > 1.0f;
			}
		}
		// Vertices are mapped once, at queue time, by the UI transform and (under
		// a target scissor, where SDL's render scale is 1) the native scale.
		const bool translate = applyTransform && (scale != 1 || offsetX != 0 || offsetY != 0);
		const bool native = physicalClip.has_value();
		const auto map = [&](float &x, float &y)
		{
			if (translate)
			{
				x = x * scale + offsetX;
				y = y * scale + offsetY;
			}
			if (native)
			{
				x *= nativeX;
				y *= nativeY;
			}
		};
		if (vertices.size() >= DirectSubmissionVertices)
		{
			// Large lists (terrain, bars) gain nothing from merging: they follow
			// all pending runs as one unindexed call, as before batching, and
			// are copied only when they need mapping.
			submitBatch();
			if (translate || native)
			{
				direct.assign(vertices.begin(), vertices.end());
				for (auto &vertex : direct)
					map(vertex.position.x, vertex.position.y);
				vertices = direct;
			}
			check(render(state, vertices, {}));
			return;
		}
		// The mapping is increasing and float rounding is monotonic, so mapping
		// the source extremes gives exactly the extremes of the mapped vertices.
		Bounds bounds{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
					  -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
		for (const auto &vertex : vertices)
		{
			bounds.left = std::min(bounds.left, vertex.position.x);
			bounds.top = std::min(bounds.top, vertex.position.y);
			bounds.right = std::max(bounds.right, vertex.position.x);
			bounds.bottom = std::max(bounds.bottom, vertex.position.y);
		}
		map(bounds.left, bounds.top);
		map(bounds.right, bounds.bottom);
		// Non-finite or inverted bounds never reorder.
		if (!(bounds.left <= bounds.right && bounds.top <= bounds.bottom) ||
			!std::isfinite(bounds.left + bounds.right + bounds.top + bounds.bottom))
			bounds = {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
					  std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
		else
			bounds = {bounds.left - boundsMargin, bounds.top - boundsMargin,
					  bounds.right + boundsMargin, bounds.bottom + boundsMargin};
		Run &run = runFor(state, bounds, vertices.size());
		const std::size_t base = run.vertices.size();
		run.vertices.insert(run.vertices.end(), vertices.begin(), vertices.end());
		if (translate || native)
			for (auto vertex = run.vertices.begin() + base; vertex != run.vertices.end(); ++vertex)
				map(vertex->position.x, vertex->position.y);
		if (!indexedBatches)
			return;
		// Mirrors SDL_SW_RenderGeometryRaw's remapping of an unindexed call: a
		// vertex equal to one of the previous triangle's reuses its index.
		const std::size_t first = run.indices.size();
		for (std::size_t i = 0; i < vertices.size(); ++i)
		{
			int index = int(base + i);
			if (i >= 3)
			{
				const int *previous = run.indices.data() + first + (i / 3 - 1) * 3;
				for (int j = 0; j < 3; ++j)
					if (sameVertex(run.vertices[previous[j]], run.vertices[index], entry != nullptr))
					{
						index = previous[j];
						break;
					}
			}
			run.indices.push_back(index);
		}
	}

	// Whether `texture` may be given new pixels in place (SDL_UpdateTexture):
	// always, except a static texture of SDL's software renderer (see
	// softwareRenderer), which is replaced by a streaming one instead.
	bool changeableTexture(SDL_Texture *texture) const
	{
		return !softwareRenderer ||
			   SDL_GetNumberProperty(SDL_GetTextureProperties(texture), SDL_PROP_TEXTURE_ACCESS_NUMBER,
									 SDL_TEXTUREACCESS_STATIC) == SDL_TEXTUREACCESS_STREAMING;
	}

  public:
	explicit SDLRenderBackend(SDL_Renderer *renderer)
		: renderer(renderer), gpuPreparation(gpuPreparationEnabled(renderer))
	{
		softwareRenderer = std::strcmp(SDL_GetRendererName(renderer), "software") == 0;
		indexedBatches = softwareRenderer;
        if (gpuPreparation) {
            gpuPreparation = exactPremultiplication();
            if (!gpuPreparation) releaseStaging();
        }
	}
	~SDLRenderBackend() override
	{
		reset();
		SDL_DestroyRenderer(renderer);
	}
	int maximumTextureSize() const override
	{
		// Fixed for the renderer's lifetime; callers ask on every sprite draw.
		if (!maximumTexture)
			maximumTexture = int(SDL_GetNumberProperty(SDL_GetRendererProperties(renderer),
													   SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0));
		return maximumTexture;
	}
	void transform(float factor, float x, float y, const SDL_Rect *output) override
	{
		if (!std::isfinite(factor) || factor <= 0 || !std::isfinite(x) || !std::isfinite(y))
			throw std::invalid_argument("Invalid UI transform");
		scale = factor;
		offsetX = x;
		offsetY = y;
		bounds = output ? std::optional<SDL_Rect>(*output) : std::nullopt;
		clip(nullptr);
	}
	void clip(const SDL_Rect *rect) override
	{
		std::optional<SDL_Rect> result = bounds;
		if (rect)
		{
			const int x = int(std::floor(rect->x * scale + offsetX)),
					  y = int(std::floor(rect->y * scale + offsetY));
			SDL_Rect mapped{x, y, int(std::ceil((rect->x + rect->w) * scale + offsetX)) - x,
							int(std::ceil((rect->y + rect->h) * scale + offsetY)) - y};
			if (bounds && !SDL_GetRectIntersection(&mapped, &*bounds, &mapped))
				mapped = {}; // SDL treats negative clip extents as clipping disabled.
			result = mapped;
		}
		if (result && (result->w <= 0 || result->h <= 0))
			result = SDL_Rect{};
		const std::optional<SDL_Rect> applied = physicalClip ? physicalClip : result;
		if (clipKnown && applied.has_value() == appliedClip.has_value() &&
			(!applied || SDL_RectsEqual(&*applied, &*appliedClip)))
			return;
		submitBatch();
		check(SDL_SetRenderClipRect(renderer, applied ? &*applied : nullptr));
		appliedClip = applied;
		clipKnown = true;
	}
	Texture &textureFor(const void *key, SDL_Surface *pixels, std::uint64_t revision,
						bool linear = false)
	{
		if (!pixels)
			throw std::invalid_argument("Texture has no CPU pixels");
		auto found = textures.find(key);
		if (found != textures.end() && found->second.revision == revision &&
			found->second.linear == linear && found->second.width == pixels->w &&
			found->second.height == pixels->h && found->second.format == pixels->format &&
			!(found->second.gpu && SDL_SurfaceHasColorKey(pixels)))
			return found->second;
		// Uploads, render-target switches and destruction must follow the queued
		// draws that sample the previous pixels.
		submitBatch();
		if (found != textures.end() &&
			(found->second.linear != linear || found->second.width != pixels->w ||
			 found->second.height != pixels->h ||
			 (found->second.gpu && SDL_SurfaceHasColorKey(pixels)) ||
			 found->second.format != pixels->format))
		{
			forget(key);
			found = textures.end();
		}
		if (found != textures.end() && found->second.revision == revision)
			return found->second;
		SDL_Texture *texture = found == textures.end() ? nullptr : found->second.handle;
		bool gpu = found != textures.end() && found->second.gpu;
		if (linear && (gpuPreparation || gpu) && pixels->format == SDL_PIXELFORMAT_ARGB8888 &&
			!SDL_SurfaceHasColorKey(pixels) && (!texture || gpu))
		{
			if (!texture)
			{
				texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
											SDL_TEXTUREACCESS_TARGET, pixels->w, pixels->h);
				++counts.textureCreates;
			}
			if (texture && SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED) &&
				SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR) &&
				premultiply(texture, pixels))
				gpu = true;
			else
			{
				if (found != textures.end())
					erase(found);
				SDL_DestroyTexture(texture);
				texture = nullptr;
				// Unsupported target/blend combinations retain the CPU path.
				gpuPreparation = false;
				gpu = false;
			}
		}
		if (!gpu)
		{
			std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> converted(
				nullptr, SDL_DestroySurface);
			if (linear)
			{
				converted.reset(SDL_ConvertSurface(pixels, SDL_PIXELFORMAT_ARGB8888));
				if (!converted || !SDL_LockSurface(converted.get()))
					throw std::runtime_error(SDL_GetError());
				for (int y = 0; y < converted->h; ++y)
					premultiplyRow(reinterpret_cast<Uint32 *>(
									   static_cast<Uint8 *>(converted->pixels) + y * converted->pitch),
								   converted->w);
				SDL_UnlockSurface(converted.get());
				counts.cpuPremultipliedBytes += std::uint64_t(converted->w) * converted->h * 4;
			}
			auto *source = linear ? converted.get() : pixels;
			// A texture that already exists is being given new pixels: its
			// surface changes, so on the software renderer it must be streaming.
			const bool changing = texture != nullptr;
			if (texture &&
				(SDL_SurfaceHasColorKey(source) || !changeableTexture(texture) ||
				 SDL_GetNumberProperty(SDL_GetTextureProperties(texture),
									   SDL_PROP_TEXTURE_FORMAT_NUMBER, 0) != source->format))
			{
				SDL_DestroyTexture(texture);
				texture = nullptr;
			}
			if (texture)
				check(update(texture, source));
			else
			{
				if (changing && softwareRenderer && !SDL_SurfaceHasColorKey(source))
				{
					texture = SDL_CreateTexture(renderer, source->format, SDL_TEXTUREACCESS_STREAMING,
												source->w, source->h);
					if (texture && !update(texture, source))
					{
						SDL_DestroyTexture(texture);
						texture = nullptr;
					}
				}
				if (!texture)
					texture = SDL_CreateTextureFromSurface(renderer, source);
				++counts.textureCreates;
			}
			if (!texture)
				throw std::runtime_error(SDL_GetError());
			++counts.textureUploads;
			counts.textureUploadBytes += std::uint64_t(source->h) * source->pitch;
			check(SDL_SetTextureBlendMode(texture, linear ? SDL_BLENDMODE_BLEND_PREMULTIPLIED
														  : SDL_BLENDMODE_BLEND));
			check(SDL_SetTextureScaleMode(texture,
										  linear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST));
		}
		auto &entry = textures[key];
		textureBytes -= entry.bytes;
		entry = {texture, revision, linear, gpu, pixels->w, pixels->h, pixels->format, pixels,
				 storageBytes(texture), std::uint64_t(pixels->h) * pixels->pitch,
				 linear ? SDL_BLENDMODE_BLEND_PREMULTIPLIED : SDL_BLENDMODE_BLEND,
				 linear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST};
		textureBytes += entry.bytes;
		peakTextureBytes = std::max(peakTextureBytes, textureBytes);
		return entry;
	}
	void prepareTexture(const void *key, SDL_Surface *pixels, std::uint64_t revision,
						bool linear = false) override
	{
		textureFor(key, pixels, revision, linear);
	}
	// Redraws `rect` of a GPU-prepared texture from the same rectangle of the
	// staging texture, exactly as premultiply() prepared the whole: clear to
	// transparent black, then blend the straight-alpha texels over it, which
	// writes colour times alpha (verified bit-exact by exactPremultiplication).
	void premultiplyStagedRegion(SDL_Texture *texture, const SDL_Rect &rect)
	{
		lastTexture = nullptr; // A target switch breaks SDL's draw batch.
		auto *target = SDL_GetRenderTarget(renderer);
		SDL_FColor color;
		SDL_BlendMode blend;
		check(SDL_GetRenderDrawColorFloat(renderer, &color.r, &color.g, &color.b, &color.a) &&
			  SDL_GetRenderDrawBlendMode(renderer, &blend));
		check(SDL_SetRenderTarget(renderer, texture));
		const SDL_FRect area{float(rect.x), float(rect.y), float(rect.w), float(rect.h)};
		const bool drawn = SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) &&
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0) && SDL_RenderFillRect(renderer, &area) &&
			SDL_RenderTexture(renderer, uploadTexture, &area, &area);
		check(SDL_SetRenderTarget(renderer, target) &&
			  SDL_SetRenderDrawColorFloat(renderer, color.r, color.g, color.b, color.a) &&
			  SDL_SetRenderDrawBlendMode(renderer, blend) && drawn);
		++counts.gpuPreparedUploads;
	}
	// Animated terrain pages rewrite a few cells per frame. Update only those
	// rectangles of a texture that holds the immediately preceding revision
	// (`previous`): any other cached state may differ outside the rectangles.
	// Mobile tiled GPUs penalize every mid-frame upload, so smaller is better.
	// Linear (premultiplied) textures get the same bytes a whole upload would:
	// premultiplyRow on the CPU, or the verified GPU blend.
	void prepareTextureRegions(const void *key, SDL_Surface *pixels, std::uint64_t revision,
							   std::uint64_t previous, std::span<const SDL_Rect> dirty,
							   bool linear = false) override
	{
		// Draws choose a texture's sampling; a refresh keeps the mode the last
		// draw used. Forcing the surface's preferred mode here made pages that
		// draw nearest alternate between two full re-creations per frame.
		// Anything this cannot patch in place is left for the next draw to
		// upload whole, as before region updates existed.
		auto found = textures.find(key);
		if (!pixels || found == textures.end() || found->second.revision != previous ||
			found->second.width != pixels->w ||
			found->second.height != pixels->h || found->second.format != pixels->format ||
			pixels->format != SDL_PIXELFORMAT_ARGB8888 || SDL_SurfaceHasColorKey(pixels) ||
			!changeableTexture(found->second.handle) ||
			SDL_GetNumberProperty(SDL_GetTextureProperties(found->second.handle),
								  SDL_PROP_TEXTURE_FORMAT_NUMBER, 0) != SDL_PIXELFORMAT_ARGB8888)
			return;
		linear = found->second.linear;
		// Queued draws sample the previous revision; submit them first.
		submitBatch();
		auto &entry = found->second;
		const SDL_Rect whole{0, 0, pixels->w, pixels->h};
		std::vector<Uint32> premultiplied;
		if (!SDL_LockSurface(pixels))
			throw std::runtime_error(SDL_GetError());
		struct Unlock
		{
			SDL_Surface *surface;
			~Unlock() { SDL_UnlockSurface(surface); }
		} unlock{pixels};
		for (SDL_Rect rect : dirty)
		{
			if (!SDL_GetRectIntersection(&rect, &whole, &rect))
				continue;
			const auto *first = static_cast<const Uint8 *>(pixels->pixels) + rect.y * pixels->pitch + rect.x * 4;
			const auto bytes = std::uint64_t(rect.w) * rect.h * 4;
			if (!linear)
				check(SDL_UpdateTexture(entry.handle, &rect, first, pixels->pitch));
			else if (!entry.gpu)
			{
				// The same integer premultiplication as a complete CPU upload.
				premultiplied.resize(std::size_t(rect.w) * rect.h);
				for (int y = 0; y < rect.h; ++y)
				{
					auto *row = premultiplied.data() + std::size_t(y) * rect.w;
					std::memcpy(row, first + y * pixels->pitch, std::size_t(rect.w) * 4);
					premultiplyRow(row, rect.w);
				}
				counts.cpuPremultipliedBytes += bytes;
				check(SDL_UpdateTexture(entry.handle, &rect, premultiplied.data(), rect.w * 4));
			}
			else
			{
				// A GPU-prepared page stages the rectangle in the shared
				// straight-alpha texture, which must match the page's size.
				uploadTexture = stagingFor(pixels->w, pixels->h);
				if (!uploadTexture)
				{
					textureFor(key, pixels, revision, linear);
					return;
				}
				check(SDL_UpdateTexture(uploadTexture, &rect, first, pixels->pitch));
				premultiplyStagedRegion(entry.handle, rect);
			}
			++counts.textureUploads;
			counts.textureUploadBytes += bytes;
		}
		entry.revision = revision;
	}
	void triangles(std::span<const SDL_Vertex> vertices, const void *key, SDL_Surface *pixels,
				   std::uint64_t revision) override
	{
		if (vertices.empty())
			return;
		if (!key)
			return queue(vertices, nullptr,
						 opaqueRectangles(vertices) ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND,
						 SDL_SCALEMODE_NEAREST, true);
		auto &entry = textureFor(key, pixels, revision);
		queue(vertices, &entry, SDL_BLENDMODE_BLEND, SDL_SCALEMODE_NEAREST, true);
	}
	// Untextured lists made only of opaque, uniformly coloured, axis-aligned
	// quads in fill()'s corner order (OpaqueRectangleBatch, bars) can replace
	// instead of blend.
	static bool opaqueRectangles(std::span<const SDL_Vertex> vertices)
	{
		if (vertices.size() % 6)
			return false;
		const auto same = [](const SDL_Vertex &a, const SDL_Vertex &b)
		{ return std::memcmp(&a, &b, sizeof(a)) == 0; };
		const auto color = [](const SDL_Vertex &a, const SDL_Vertex &b)
		{ return std::memcmp(&a.color, &b.color, sizeof(a.color)) == 0; };
		for (std::size_t i = 0; i < vertices.size(); i += 6)
		{
			const auto *q = vertices.data() + i;
			if (q[0].color.a != 1.0f || !same(q[3], q[0]) || !same(q[4], q[2]) ||
				!color(q[1], q[0]) || !color(q[2], q[0]) || !color(q[5], q[0]) ||
				q[1].position.y != q[0].position.y || q[2].position.x != q[1].position.x ||
				q[5].position.x != q[0].position.x || q[5].position.y != q[2].position.y ||
				!(q[0].position.x < q[1].position.x) || !(q[0].position.y < q[2].position.y) ||
				!std::isfinite(q[2].position.x + q[2].position.y + q[0].position.x + q[0].position.y))
				return false;
		}
		return true;
	}
	void screenTriangles(std::span<const SDL_Vertex> vertices) override
	{
		queue(vertices, nullptr, SDL_BLENDMODE_BLEND, SDL_SCALEMODE_NEAREST, false);
	}
	static void quad(SDL_Vertex (&vertices)[6], const SDL_Surface *pixels, const SDL_Rect &src,
					 const SDL_FRect &dst, SDL_FColor c)
	{
		quad(vertices, pixels->w, pixels->h, src, dst, c);
	}
	static void quad(SDL_Vertex (&vertices)[6], int width, int height, const SDL_Rect &src,
					 const SDL_FRect &dst, SDL_FColor c)
	{
		const float u0 = float(src.x) / width, v0 = float(src.y) / height;
		const float u1 = float(src.x + src.w) / width, v1 = float(src.y + src.h) / height;
		const SDL_Vertex a{{dst.x, dst.y}, c, {u0, v0}}, b{{dst.x + dst.w, dst.y}, c, {u1, v0}},
			d{{dst.x, dst.y + dst.h}, c, {u0, v1}}, e{{dst.x + dst.w, dst.y + dst.h}, c, {u1, v1}};
		vertices[0] = a; vertices[1] = b; vertices[2] = e;
		vertices[3] = a; vertices[4] = e; vertices[5] = d;
	}
	bool hasTexture(const void *key, std::uint64_t revision) const override
	{
		const auto found = textures.find(key);
		return found != textures.end() && found->second.revision == revision;
	}
	bool blitCached(const void *key, const SDL_Rect &src, const SDL_FRect &dst, Uint8 alpha) override
	{
		auto found = textures.find(key);
		if (found == textures.end())
			return false;
		auto &entry = found->second;
		++counts.blits;
		SDL_Vertex vertices[6];
		if (entry.linear)
		{
			const float opacity = alpha / 255.f;
			quad(vertices, entry.width, entry.height, src, dst, {opacity, opacity, opacity, opacity});
			queue(vertices, &entry, SDL_BLENDMODE_BLEND_PREMULTIPLIED, SDL_SCALEMODE_LINEAR, true);
		}
		else
		{
			quad(vertices, entry.width, entry.height, src, dst, {1, 1, 1, alpha / 255.0f});
			queue(vertices, &entry, SDL_BLENDMODE_BLEND, SDL_SCALEMODE_NEAREST, true);
		}
		return true;
	}
	void blit(const void *key, SDL_Surface *pixels, std::uint64_t revision, bool,
			  const SDL_Rect &src, const SDL_FRect &dst, Uint8 alpha) override
	{
		++counts.blits;
		SDL_Vertex vertices[6];
		quad(vertices, pixels, src, dst, {1, 1, 1, alpha / 255.0f});
		auto &entry = textureFor(key, pixels, revision);
		queue(vertices, &entry, SDL_BLENDMODE_BLEND, SDL_SCALEMODE_NEAREST, true);
	}
	bool blitAdditive(const void *key, SDL_Surface *pixels, std::uint64_t revision,
					  const SDL_Rect &src, const SDL_FRect &dst, Uint8 alpha) override
	{
		// The straight-alpha texture with SDL's additive mode adds colour times
		// alpha. The batch applies these modes to the shared texture when it is
		// submitted; ordinary draws of the texture restore its own modes.
		++counts.blits;
		SDL_Vertex vertices[6];
		quad(vertices, pixels, src, dst, {1, 1, 1, alpha / 255.0f});
		auto &entry = textureFor(key, pixels, revision, false);
		queue(vertices, &entry, SDL_BLENDMODE_ADD, SDL_SCALEMODE_LINEAR, true);
		return true;
	}
	void blitLinear(const void *key, SDL_Surface *pixels, std::uint64_t revision,
					const SDL_Rect &src, const SDL_FRect &dst, Uint8 alpha) override
	{
		++counts.blits;
		const float opacity = alpha / 255.f;
		SDL_Vertex vertices[6];
		quad(vertices, pixels, src, dst, {opacity, opacity, opacity, opacity});
		auto &entry = textureFor(key, pixels, revision, true);
		queue(vertices, &entry, SDL_BLENDMODE_BLEND_PREMULTIPLIED, SDL_SCALEMODE_LINEAR, true);
	}
	void fill(const SDL_FRect &rect, SDL_Color color) override
	{
		const SDL_FColor c{color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
		++counts.fills;
		const SDL_Vertex a{{rect.x, rect.y}, c, {0, 0}}, b{{rect.x + rect.w, rect.y}, c, {0, 0}},
			d{{rect.x, rect.y + rect.h}, c, {0, 0}},
			e{{rect.x + rect.w, rect.y + rect.h}, c, {0, 0}};
		const SDL_Vertex vertices[] = {a, b, e, a, e, d};
		// Only positive, finite rectangles keep fill()'s corner order after
		// mapping, which the opaque path relies on.
		const bool opaque = color.a == 255 && rect.w > 0 && rect.h > 0 &&
							std::isfinite(rect.x + rect.y + rect.w + rect.h);
		queue(vertices, nullptr, opaque ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND,
			  SDL_SCALEMODE_NEAREST, true);
	}
	TextureMemory textureMemory() const override { return {textureBytes, peakTextureBytes}; }
	RenderOperations operations() const override
	{
		RenderOperations result = counts;
		result.textureCount = textures.size();
		result.textureBytes = textureBytes;
		result.peakTextureBytes = peakTextureBytes;
		std::unordered_map<const SDL_Surface *, unsigned> sources;
		for (const auto &[key, texture] : textures)
		{
			result.sourceSurfaceBytes += texture.sourceBytes;
			result.gpuPreparedTextures += texture.gpu;
			++sources[texture.source];
		}
		for (const auto &[source, uses] : sources)
			result.sharedSourceTextures += uses > 1 ? uses : 0;
		for (const auto &entry : staging)
			result.stagingTextureBytes += storageBytes(entry.texture);
		result.batchCapacityBytes = direct.capacity() * sizeof(SDL_Vertex);
		for (const auto &run : runs)
			result.batchCapacityBytes += run.vertices.capacity() * sizeof(SDL_Vertex) +
											   run.indices.capacity() * sizeof(int);
		return result;
	}
	void forget(const void *key) override
	{
		auto found = textures.find(key);
		if (found != textures.end())
		{
			submitBatch();
			SDL_DestroyTexture(found->second.handle);
			erase(found);
		}
	}
	void reset() override
	{
		submitBatch();
		for (auto [key, texture] : textures)
			SDL_DestroyTexture(texture.handle);
		counts.textureDestroys += textures.size();
		textures.clear();
		textureBytes = 0;
		lastTexture = nullptr;
		releaseStaging();
		gpuPreparation = gpuPreparationEnabled(renderer);
	}
	void clear()
	{
		check(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
		check(SDL_RenderClear(renderer));
	}
	void logicalSize(int width, int height) override
	{
		submitBatch();
		check(SDL_SetRenderLogicalPresentation(renderer, width, height,
											   SDL_LOGICAL_PRESENTATION_LETTERBOX));
		refreshBoundsMargin();
		clear();
	}
	void nativeLogicalSize(int width, int height) override
	{
		int pixelsW, pixelsH;
		outputSize(pixelsW, pixelsH);
		submitBatch();
		check(SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED));
		nativeX = float(pixelsW) / width; nativeY = float(pixelsH) / height;
        check(SDL_SetRenderScale(renderer, physicalClip ? 1 : nativeX, physicalClip ? 1 : nativeY));
		refreshBoundsMargin();
		// Lazy CPU geometry may be created after direct writes. Scale setup
		// must not clear the borrowed target or overwrite earlier layers.
	}
    void targetScissor(const SDL_Rect *rect)
    {
        if (physicalClip.has_value() == bool(rect) && (!rect ||
            (physicalClip->x==rect->x && physicalClip->y==rect->y && physicalClip->w==rect->w && physicalClip->h==rect->h))) return;
        flush(); // Previously queued geometry retains its scale and clip.
        physicalClip = rect ? std::optional<SDL_Rect>(*rect) : std::nullopt;
        check(SDL_SetRenderScale(renderer, physicalClip ? 1 : nativeX, physicalClip ? 1 : nativeY));
        check(SDL_SetRenderClipRect(renderer, rect));
        clipKnown = false;
        refreshBoundsMargin();
    }
	void flush() override
	{
		submitBatch();
		check(SDL_FlushRenderer(renderer));
	}
	void discardFrame() noexcept override
	{
		discardRuns();
	}
	void setPresentationHeld(bool held) override { presentationHeld = held; }
	bool presentationHeld = false;
	void present() override
	{
		submitBatch();
		lastTexture = nullptr;
		// A held frame completes its drawing but leaves the window's image.
		if (presentationHeld) check(SDL_FlushRenderer(renderer));
		else SDL_RenderPresent(renderer);
		// Staging textures (a page's can be 16 MiB) go after 300 frames unused.
		++presents;
		std::erase_if(staging, [&](const Staging &entry)
		{
			if (entry.presented + 300 >= presents) return false;
			if (uploadTexture == entry.texture) uploadTexture = nullptr;
			SDL_DestroyTexture(entry.texture);
			return true;
		});
		refreshBoundsMargin(); // The window may have been resized.
		clear();
	}
	void outputSize(int &width, int &height) override
	{
		check(SDL_GetRenderOutputSize(renderer, &width, &height));
	}
	SDL_Surface *capture() override
	{
		submitBatch();
		SDL_Surface *pixels = SDL_RenderReadPixels(renderer, nullptr);
		if (!pixels)
			throw std::runtime_error(SDL_GetError());
		return pixels;
	}
};
} // namespace
std::unique_ptr<RenderBackend> makeSDLRenderBackend(SDL_Window *window, int width, int height)
{
	std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateRenderer(window, nullptr), SDL_DestroyRenderer);
	if (!renderer)
		return {};
	auto backend = std::make_unique<SDLRenderBackend>(renderer.get());
    auto *active = renderer.release();
	backend->logicalSize(width, height);
	check(SDL_SetRenderDrawBlendMode(active, SDL_BLENDMODE_BLEND));
	return backend;
}

void setSDLSoftwareTargetScissor(RenderBackend &backend, const SDL_Rect *rect)
{
    auto *value = dynamic_cast<SDLRenderBackend *>(&backend);
    if (!value) throw std::logic_error("Software geometry backend missing");
    value->targetScissor(rect);
}
std::unique_ptr<RenderBackend> makeSDLSoftwareGeometryBackend(SDL_Surface *surface)
{
	std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateSoftwareRenderer(surface), SDL_DestroyRenderer);
	if (!renderer)
		throw std::runtime_error(SDL_GetError());
	auto backend = std::make_unique<SDLRenderBackend>(renderer.get());
    auto *active = renderer.release();
	check(SDL_SetRenderDrawBlendMode(active, SDL_BLENDMODE_BLEND));
	return backend;
}

} // namespace GAGCore
