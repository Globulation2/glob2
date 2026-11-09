// SPDX-License-Identifier: GPL-3.0-or-later
#include "IntroCover.h"

#include "GraphicContext.h"
#include "SimplexNoise.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>

using namespace GAGCore;

namespace
{
constexpr float Pi = 3.14159265f;

float smoothstep(float e0, float e1, float x)
{
	const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
	return t * t * (3 - 2 * t);
}

float ease(float t)
{
	t = std::clamp(t, 0.f, 1.f);
	return t * t * (3 - 2 * t);
}

//! Room for a player's team-colour dot before their name.
int dotSpace(Font *font) { return font->getStringHeight("Ag") * 3 / 4; }

//! v in [0, 1] as a byte, rounding halves up like std::lround without its libm call.
unsigned char unitToByte(float v)
{
	const float scaled = std::clamp(v, 0.f, 1.f) * 255;
	const int whole = int(scaled);
	return static_cast<unsigned char>(whole + (scaled - float(whole) >= .5f));
}

//! A non-negative value in [0, 255] rounded like std::lround.
Uint32 roundToByte(float v)
{
	const int whole = int(v);
	return Uint32(whole + (v - float(whole) >= .5f));
}

//! A lattice cell and the weight of its far side along one axis.
struct LatticeStep
{
	int index;
	float t;
};

LatticeStep latticeStep(float v, float origin, int cell, int cells)
{
	const float f = std::clamp((v - origin) / cell, 0.f, float(cells - 1.001f));
	const int index = int(f);
	return {index, f - index};
}

//! Bits of a finite float that sort in the same order as its value; both zeros map alike.
uint32_t orderedBits(float v)
{
	const uint32_t bits = std::bit_cast<uint32_t>(v + 0.f);
	return bits & 0x80000000u ? ~bits : bits | 0x80000000u;
}

//! A texture sampled smoothly when stretched, like generated HD terrain pages.
class SmoothSurface : public DrawableSurface
{
public:
	SmoothSurface(int w, int h) : DrawableSurface(w, h) { highResolutionSampling = true; }
};

int texelsFor(int pixels)
{
	// A power of two, so the texture samples smoothly with mipmaps everywhere.
	int size = 64;
	while (size < pixels / 3 && size < 512)
		size *= 2;
	return size;
}

//! Five octaves of the game's own simplex noise, roughly in [-1, 1]. `unit` is
//! the size in pixels of one lattice cell of the largest octave.
float billows(float x, float y, float unit, int z)
{
	float sum = 0, amplitude = 1, norm = 0, scale = 256 / unit;
	for (int octave = 0; octave < 5; ++octave)
	{
		// Offset each octave so their lattices do not line up.
		const float o = 1000.f * octave;
		sum += amplitude * (SimplexNoise::getNoise3D(int((x + o) * scale), int((y - o) * scale), z + 97 * octave) - 128) / 128.f;
		norm += amplitude;
		amplitude *= .55f;
		scale *= 2;
	}
	return sum / norm;
}
}

//! Billowy noise: the sum of |noise| over octaves, in about [0, 1]. Each
//! octave's zero crossings become sharp creases between rounded puffs, the
//! cauliflower look of cumulus seen from above.
float puffs(float x, float y, float unit, int z)
{
	float sum = 0, amplitude = 1, norm = 0, scale = 256 / unit;
	for (int octave = 0; octave < 4; ++octave)
	{
		const float o = 3000.f + 1000.f * octave;
		const float n = (SimplexNoise::getNoise3D(int((x + o) * scale), int((y + o) * scale), z + 53 * octave) - 128) / 128.f;
		sum += amplitude * std::fabs(n);
		norm += amplitude;
		amplitude *= .5f;
		scale *= 2.1f;
	}
	return std::min(1.f, 1.6f * sum / norm);
}


void IntroCover::reset()
{
	*this = IntroCover();
}

void IntroCover::begin(Uint64 now, Title title)
{
	*this = IntroCover();
	start = std::max<Uint64>(now, 1);
	this->title = std::move(title);
}

void IntroCover::skip(Uint64 now)
{
	// Part from here, a little faster than usual.
	if (!start || now - start >= partFrom)
		return;
	partFrom = Uint32(now - start);
	end = partFrom + PART_MS / 2;
}

float IntroCover::titleOpacity(Uint64 now) const
{
	// Already there with the bank, like the clouds; gone early in the parting.
	return active(now) ? 1 - ease(ease(part(now)) / .18f) : 0;
}

float IntroCover::part(Uint64 now) const
{
	if (!start || now - start < partFrom)
		return 0;
	return std::clamp(float(now - start - partFrom) / float(end - partFrom), 0.f, 1.f);
}

IntroCover::Threshold IntroCover::threshold(float part, const Depth &depth)
{
	// The centre clears first; the threshold passes every rank by part = 1.
	part = std::min(1.f, part * depth.lead);
	return {-.55f + 1.75f * part, .18f * std::sin(Pi * part)};
}

void IntroCover::computeLattice(int w, int h, double seconds, float part, Lattice &out, const Depth &depth)
{
	const float s = std::min(w, h) / 540.f; // offsets and drift scale with the screen
	part = std::min(1.f, part * depth.lead);

	// Sample the noise on a screen-space lattice, with margins for the
	// offsets. The view drops through the bank as it parts, so the layer grows
	// from the centre of the screen; it drifts with the wind throughout.
	out.w = w;
	out.h = h;
	out.cell = std::max(5, std::min(w, h) / 110);
	// The shadow falls down and to the right; the light comes from up and left.
	out.shadowX = 30 * s, out.shadowY = 40 * s, out.lightX = 7 * s, out.lightY = 9 * s;
	const int cell = out.cell;
	const float margin = out.shadowY + out.lightY + cell;
	out.ox = -margin, out.oy = -margin;
	const int cw = out.cw = int((w + 2 * margin) / cell) + 3, ch = out.ch = int((h + 2 * margin) / cell) + 3;
	const float cx = w / 2.f, cy = h / 2.f;
	const float k = std::exp(depth.zoom * part * part);
	const float windX = float(22 * s * seconds * depth.wind), windY = float(9 * s * seconds * depth.wind);
	const float unit = .45f * depth.size * std::min(w, h);
	const int z = int(seconds * 8) + depth.seed;
	std::vector<float> &ranks = out.ranks, &heights = out.heights;
	ranks.resize(size_t(cw) * ch);
	heights.resize(size_t(cw) * ch);
	for (int j = 0; j < ch; ++j)
		for (int i = 0; i < cw; ++i)
		{
			const float x = out.ox + float(i) * cell, y = out.oy + float(j) * cell;
			const float nx = cx + (x - cx) / k + windX + 5000.f * depth.seed, ny = cy + (y - cy) / k + windY;
			// Puffs both shade the cloud and bite into its edges.
			const float h = puffs(nx, ny, .3f * unit, z);
			heights[size_t(j) * cw + i] = h;
			ranks[size_t(j) * cw + i] = billows(nx, ny, unit, z) + .35f * h;
		}
	// Equalise to ranks in [0, 1]: raising the threshold then clears sky at a
	// steady rate, whatever the noise's distribution. A value's rank is the
	// number of values below it, so equal values share the rank of the first.
	// One sort of (value, index) keys gives every rank without a search each.
	const size_t count = ranks.size();
	std::vector<uint64_t> keys(count);
	for (size_t at = 0; at < count; ++at)
		keys[at] = uint64_t(orderedBits(ranks[at])) << 32 | at;
	std::sort(keys.begin(), keys.end());
	const float n = float(count);
	for (size_t first = 0, at = 0; at < count; ++at)
	{
		if (uint32_t(keys[at] >> 32) != uint32_t(keys[first] >> 32))
			first = at;
		ranks[keys[at] & 0xffffffffu] = float(first) / n;
	}
}

void IntroCover::computeLayers(const Lattice &lattice, Threshold threshold, Layers &out)
{
	const int w = lattice.w, h = lattice.h, cw = lattice.cw, cell = lattice.cell;
	const float cx = w / 2.f, cy = h / 2.f;

	// The threshold is applied per texel of a small texture that is drawn
	// stretched with smooth filtering, so cloud edges are curves, not steps.
	out.w = texelsFor(w);
	out.h = texelsFor(h);
	out.shadow.resize(size_t(out.w) * out.h);
	out.body.resize(size_t(out.w) * out.h);
	out.light.resize(size_t(out.w) * out.h);
	const float maxDistance = std::hypot(cx, cy);
	const float base = threshold.base, bias = threshold.bias;

	// Each texel interpolates the lattice at its centre and at the shadow and
	// light offsets around it. Those positions depend on only one axis each,
	// so the cells and weights are found once per column and once per row.
	struct Along
	{
		std::vector<LatticeStep> at, shadow, ahead, behind; // centre, -shadow, +light, -light
		std::vector<float> squared;                         // squared distance from the screen's centre
	};
	const auto along = [&](int texels, int pixels, float origin, int cells, float shadowOffset, float lightOffset,
	                       float centre)
	{
		Along a;
		for (std::vector<LatticeStep> *steps : {&a.at, &a.shadow, &a.ahead, &a.behind})
			steps->resize(size_t(texels));
		a.squared.resize(size_t(texels));
		for (int t = 0; t < texels; ++t)
		{
			const float v = (t + .5f) * pixels / texels;
			a.at[t] = latticeStep(v, origin, cell, cells);
			a.shadow[t] = latticeStep(v - shadowOffset, origin, cell, cells);
			a.ahead[t] = latticeStep(v + lightOffset, origin, cell, cells);
			a.behind[t] = latticeStep(v - lightOffset, origin, cell, cells);
			a.squared[t] = (v - centre) * (v - centre);
		}
		return a;
	};
	const Along columns = along(out.w, w, lattice.ox, cw, lattice.shadowX, lattice.lightX, cx);
	const Along rows = along(out.h, h, lattice.oy, lattice.ch, lattice.shadowY, lattice.lightY, cy);
	const auto lerp = [cw](const float *field, LatticeStep x, LatticeStep y)
	{
		const float *row = field + size_t(y.index) * cw + x.index;
		return (row[0] * (1 - x.t) + row[1] * x.t) * (1 - y.t) + (row[cw] * (1 - x.t) + row[cw + 1] * x.t) * y.t;
	};
	const float *rankField = lattice.ranks.data(), *heightField = lattice.heights.data();
	for (int j = 0; j < out.h; ++j)
	{
		const LatticeStep y = rows.at[j], yShadow = rows.shadow[j], yAhead = rows.ahead[j], yBehind = rows.behind[j];
		const float dy2 = rows.squared[j];
		unsigned char *shadowRow = &out.shadow[size_t(j) * out.w], *bodyRow = &out.body[size_t(j) * out.w],
		              *lightRow = &out.light[size_t(j) * out.w];
		for (int i = 0; i < out.w; ++i)
		{
			const LatticeStep x = columns.at[i];
			const float th = base + (1 - std::sqrt(columns.squared[i] + dy2) / maxDistance) * bias;
			const float u = lerp(rankField, x, y);
			// Thin, translucent fringes; dense where the cloud is deep.
			// Density thins gradually into wisps rather than ending at an edge.
			const float a = smoothstep(th - .15f, th + .35f, u);
			const float shadow = smoothstep(th - .15f, th + .35f, lerp(rankField, columns.shadow[i], yShadow));
			// Light from the upper left wraps over each puff: the side facing
			// it is bright, the far side and the creases are softly shaded,
			// and thin edges are greyer than the deep middle.
			const float hc = lerp(heightField, x, y);
			const float slope = lerp(heightField, columns.ahead[i], yAhead) - lerp(heightField, columns.behind[i], yBehind);
			const float tone = std::clamp(.5f + .3f * hc + 1.2f * slope + .25f * smoothstep(th, th + .5f, u) - .1f,
			                              0.f, 1.f);
			shadowRow[i] = unitToByte(.35f * shadow);
			bodyRow[i] = unitToByte(a);
			lightRow[i] = unitToByte(a * tone);
		}
	}
}

void IntroCover::computeLayers(int w, int h, double seconds, float part, Layers &out, const Depth &depth)
{
	Lattice lattice;
	computeLattice(w, h, seconds, part, lattice, depth);
	computeLayers(lattice, threshold(part, depth), out);
}

void IntroCover::computeLayers(int w, int h, double seconds, float part, Layers &out)
{
	computeLayers(w, h, seconds, part, out, Depth());
}

void IntroCover::composite(int w, int h, float part, const Layers &nearLayers, const Layers &farLayers,
                           const Shade &shade, Uint32 *pixels, int pitch)
{
	// Composite both layers (each a shadow, then cloud graded from shaded
	// creases to sunlit tops) and the title's shade into one straight-alpha
	// texture, as if each were drawn over the last.
	const int tw = nearLayers.w, th = nearLayers.h;
	const float nearOpacity = .7f * (1 - .5f * part), farOpacity = .67f * (1 - .5f * part);
	// The title's shade depends on the distance from the title, one axis at a time.
	std::vector<float> shadeX(shade.alpha > 0 ? size_t(tw) : 0), shadeY(shade.alpha > 0 ? size_t(th) : 0);
	for (int i = 0; i < int(shadeX.size()); ++i)
	{
		const float d = ((i + .5f) * w / tw - shade.cx) / shade.rx;
		shadeX[i] = d * d;
	}
	for (int j = 0; j < int(shadeY.size()); ++j)
	{
		const float d = ((j + .5f) * h / th - shade.cy) / shade.ry;
		shadeY[j] = d * d;
	}
	constexpr float byte = 1 / 255.f;
	for (int j = 0; j < th; ++j)
	{
		auto *row = reinterpret_cast<Uint32 *>(reinterpret_cast<Uint8 *>(pixels) + size_t(j) * pitch);
		for (int i = 0; i < tw; ++i)
		{
			const size_t at = size_t(j) * tw + i;
			float a = 0, r = 0, g = 0, b = 0; // premultiplied
			const auto over = [&](float alpha, float cr, float cg, float cb)
			{
				r = r * (1 - alpha) + cr * alpha;
				g = g * (1 - alpha) + cg * alpha;
				b = b * (1 - alpha) + cb * alpha;
				a = a + alpha * (1 - a);
			};
			const auto cloud = [&](const Layers &l, float haze, float opacity)
			{
				const unsigned bodyByte = l.body[at];
				if (!bodyByte)
					return;
				const float t = float(l.light[at]) / float(bodyByte);
				// Crease blue-grey, through a cool mid tone, to warm sunlit white.
				float cr, cg, cb;
				if (t < .5f)
				{
					const float q = t * 2;
					cr = 176 + (218 - 176) * q, cg = 182 + (222 - 182) * q, cb = 196 + (230 - 196) * q;
				}
				else
				{
					const float q = (t - .5f) * 2;
					cr = 218 + (250 - 218) * q, cg = 222 + (250 - 222) * q, cb = 230 + (248 - 230) * q;
				}
				cr += (176 - cr) * haze, cg += (188 - cg) * haze, cb += (212 - cb) * haze;
				over(bodyByte * byte * opacity, cr, cg, cb);
			};
			// Two translucent layers hide about 90% of the map while the
			// renderer settles underneath, then fly past.
			over(farLayers.shadow[at] * (byte * .35f), 20, 28, 44);
			cloud(farLayers, .3f, farOpacity);
			over(nearLayers.shadow[at] * (byte * .5f), 20, 28, 44);
			cloud(nearLayers, 0, nearOpacity);
			if (shade.alpha > 0)
				over(.32f * shade.alpha * smoothstep(1.2f, .2f, std::sqrt(shadeX[i] + shadeY[j])), 52, 64, 88);
			const float inv = a > 0 ? 1 / a : 0;
			row[i] = Uint32(unitToByte(a)) << 24 | roundToByte(std::min(255.f, r * inv)) << 16 |
			         roundToByte(std::min(255.f, g * inv)) << 8 | roundToByte(std::min(255.f, b * inv));
		}
	}
}

IntroCover::TitleLayout IntroCover::layoutTitle(int w, int h, Font *titleFont, Font *playerFont) const
{
	TitleLayout layout;
	if (!titleFont || title.map.empty())
		return layout;
	// Lay the player names out in centred rows that fit the screen.
	layout.gap = playerFont ? std::max(18, playerFont->getStringWidth("    ")) : 0;
	layout.rows.emplace_back();
	layout.widths.push_back(0);
	if (playerFont)
		for (const Player &player : title.players)
		{
			const int pw = dotSpace(playerFont) + playerFont->getStringWidth(player.name);
			if (!layout.rows.back().empty() && layout.widths.back() + layout.gap + pw > w * 9 / 10)
			{
				layout.rows.emplace_back();
				layout.widths.push_back(0);
			}
			layout.widths.back() += (layout.rows.back().empty() ? 0 : layout.gap) + pw;
			layout.rows.back().push_back(&player);
		}
	layout.titleW = titleFont->getStringWidth(title.map);
	layout.titleH = titleFont->getStringHeight(title.map);
	layout.rowH = playerFont ? playerFont->getStringHeight("Ag") + 4 : 0;
	const int playersH = title.players.empty() ? 0 : int(layout.rows.size()) * layout.rowH;
	layout.blockW = std::max(layout.titleW, *std::max_element(layout.widths.begin(), layout.widths.end()));
	layout.blockH = layout.titleH + (playersH ? 14 + playersH : 0);
	layout.top = int(h * .4f) - layout.blockH / 2;
	return layout;
}

namespace
{
//! Makes one layer's lattices on a thread of its own, a frame ahead of drawing.
class LatticeWorker
{
public:
	explicit LatticeWorker(IntroCover::Depth depth) : depth(depth), thread([this] { run(); }) {}
	~LatticeWorker()
	{
		{
			std::lock_guard lock(mutex);
			stopping = true;
		}
		wake.notify_one();
		thread.join();
	}
	LatticeWorker(const LatticeWorker &) = delete;
	LatticeWorker &operator=(const LatticeWorker &) = delete;

	//! Ask for the lattice at this moment, replacing a request not yet started.
	void request(int w, int h, double seconds, float part)
	{
		{
			std::lock_guard lock(mutex);
			pending = Request{w, h, seconds, part};
		}
		wake.notify_one();
	}
	//! Swap in the newest finished lattice, if there is one not taken yet;
	//! with `wait`, wait for the one being made.
	bool take(IntroCover::Lattice &into, bool wait = false)
	{
		std::unique_lock lock(mutex);
		if (wait)
			finished.wait(lock, [&] { return fresh || (!pending && !busy); });
		if (!fresh)
			return false;
		std::swap(into, done);
		fresh = false;
		return true;
	}
	//! Nothing asked for is still being made.
	bool idle()
	{
		std::lock_guard lock(mutex);
		return !pending && !busy;
	}

private:
	struct Request
	{
		int w, h;
		double seconds;
		float part;
	};
	void run()
	{
		IntroCover::Lattice working;
		std::unique_lock lock(mutex);
		for (;;)
		{
			wake.wait(lock, [&] { return stopping || pending.has_value(); });
			if (stopping)
				return;
			const Request next = *pending;
			pending.reset();
			busy = true;
			lock.unlock();
			bool made = true;
			try
			{
				IntroCover::computeLattice(next.w, next.h, next.seconds, next.part, working, depth);
			}
			catch (const std::bad_alloc &)
			{
				made = false; // the frame keeps its previous lattice
			}
			lock.lock();
			busy = false;
			if (made)
			{
				std::swap(done, working);
				fresh = true;
			}
			finished.notify_all();
		}
	}

	const IntroCover::Depth depth;
	std::mutex mutex;
	std::condition_variable wake, finished;
	std::optional<Request> pending;
	IntroCover::Lattice done;
	bool fresh = false, busy = false, stopping = false;
	std::thread thread; // last, so it starts with everything above in place
};
}

struct IntroCover::Workers
{
	LatticeWorker nearWorker{nearDepth()}, farWorker{farDepth()};
	void take(Lattice &nearLattice, Lattice &farLattice, bool wait)
	{
		nearWorker.take(nearLattice, wait);
		farWorker.take(farLattice, wait);
	}
	bool idle() { return nearWorker.idle() && farWorker.idle(); }
	void request(int w, int h, double seconds, float part)
	{
		nearWorker.request(w, h, seconds, part);
		farWorker.request(w, h, seconds, part);
	}
};

IntroCover::IntroCover() = default;
IntroCover::~IntroCover() = default;
IntroCover::IntroCover(IntroCover &&) noexcept = default;
IntroCover &IntroCover::operator=(IntroCover &&) noexcept = default;

bool IntroCover::startWorkers()
{
	if (!workers)
	{
		try
		{
			workers = std::make_unique<Workers>();
		}
		catch (const std::system_error &)
		{
			return false; // no threads: each frame makes its own lattices
		}
	}
	return true;
}

void IntroCover::prepare(DrawableSurface &gfx, int w, int h)
{
	if (!start || w <= 0 || h <= 0)
		return;
	// Link the program now rather than in the first frame, and start the
	// first frame's lattices; the cover stays closed for a second anyway.
	if (auto *context = dynamic_cast<GraphicContext *>(&gfx))
		context->prepareCloudCover();
	if (startWorkers())
		workers->request(w, h, 0, 0);
}

void IntroCover::requestNext(int w, int h, Uint64 now)
{
	// The next frame comes about one frame interval from now.
	const Uint64 interval = lastDraw && now > lastDraw ? std::min<Uint64>(now - lastDraw, 50) : 8;
	lastDraw = now;
	const Uint64 next = now + interval;
	if (next - start >= end)
	{
		// The bank is gone by then: nothing more to make. Let the threads go
		// once they have finished, so that no frame waits for them.
		if (workers && workers->idle())
			workers.reset();
		return;
	}
	if (startWorkers())
		workers->request(w, h, double(next - start) / 1000, ease(part(next)));
}

CloudCoverFrame IntroCover::perPixelFrame(const Lattice &nearLattice, const Lattice &farLattice, float part,
                                          const Shade &shade, std::vector<Uint16> &packed)
{
	CloudCoverFrame frame;
	const Lattice &n = nearLattice, &f = farLattice;
	if (n.cw != f.cw || n.ch != f.ch || n.cell != f.cell || n.w != f.w || n.h != f.h || n.cw < 2 || n.ch < 2)
		return frame; // no lattice: nothing to draw
	// Both layers in one texture: near rank and height, far rank and height.
	const size_t points = size_t(n.cw) * n.ch;
	packed.resize(points * 4);
	const auto unit = [](float v) { return Uint16(std::clamp(v, 0.f, 1.f) * 65535 + .5f); };
	for (size_t at = 0; at < points; ++at)
	{
		packed[at * 4 + 0] = unit(n.ranks[at]);
		packed[at * 4 + 1] = unit(n.heights[at]);
		packed[at * 4 + 2] = unit(f.ranks[at]);
		packed[at * 4 + 3] = unit(f.heights[at]);
	}
	frame.lattice = packed.data();
	frame.latticeW = n.cw;
	frame.latticeH = n.ch;
	frame.originX = n.ox;
	frame.originY = n.oy;
	frame.cell = float(n.cell);
	frame.width = float(n.w);
	frame.height = float(n.h);
	frame.centreX = n.w / 2.f;
	frame.centreY = n.h / 2.f;
	frame.maxDistance = std::hypot(frame.centreX, frame.centreY);
	frame.shadowX = n.shadowX;
	frame.shadowY = n.shadowY;
	frame.lightX = n.lightX;
	frame.lightY = n.lightY;
	// The same thresholds, opacities, haze and shadows as computeLayers() and composite().
	const Threshold nearThreshold = threshold(part, nearDepth()), farThreshold = threshold(part, farDepth());
	frame.nearLayer = {nearThreshold.base, nearThreshold.bias, .7f * (1 - .5f * part), 0, .5f};
	frame.farLayer = {farThreshold.base, farThreshold.bias, .67f * (1 - .5f * part), .3f, .35f};
	frame.shadeAlpha = shade.alpha;
	frame.shadeX = shade.cx;
	frame.shadeY = shade.cy;
	frame.shadeRX = shade.rx;
	frame.shadeRY = shade.ry;
	return frame;
}

void IntroCover::draw(DrawableSurface &gfx, int w, int h, Uint64 now, Font *titleFont, Font *playerFont, bool clouds)
{
	if (!active(now) || w <= 0 || h <= 0)
		return;
	const float p = ease(part(now));
	const float titleAlpha = titleOpacity(now);
	const TitleLayout layout = layoutTitle(w, h, titleFont, playerFont);
	if (!clouds)
	{
		// Without cloud layers (the software renderer) or with reduced motion, a plain sky fades out.
		gfx.drawFilledRect(0, 0, w, h, 214, 220, 232, Uint8(std::lround(255 * (1 - p))));
		gfx.drawFilledRect(0, 0, w, h, 28, 38, 52, Uint8(std::lround(90 * titleAlpha)));
	}
	else
	{
		const double seconds = double(now - start) / 1000;
		const Depth nearSettings = nearDepth(), farSettings = farDepth();
		// The workers make each frame's lattices while the frame before is
		// drawn; the first frame, or one after a resize, makes its own.
		const auto fits = [&](const Lattice &l) { return l.w == w && l.h == h; };
		if (workers)
			workers->take(nearLattice, farLattice, !fits(nearLattice) || !fits(farLattice));
		if (!fits(nearLattice) || !fits(farLattice))
		{
			computeLattice(w, h, seconds, p, nearLattice, nearSettings);
			computeLattice(w, h, seconds, p, farLattice, farSettings);
		}
		requestNext(w, h, now);

		Shade shade;
		if (titleAlpha > 0 && layout.blockW)
		{
			shade.alpha = titleAlpha;
			shade.cx = w / 2.f;
			shade.cy = layout.top + layout.blockH / 2.f;
			shade.rx = layout.blockW / 2.f + .12f * w;
			shade.ry = layout.blockH / 2.f + .12f * h;
		}
		if (!textureFallback)
		{
			auto *context = dynamic_cast<GraphicContext *>(&gfx);
			const CloudCoverFrame frame = perPixelFrame(nearLattice, farLattice, p, shade, packed);
			if (context && frame.lattice && context->drawCloudCover(frame))
			{
				if (titleAlpha > 0)
					drawTitle(gfx, w, layout, titleAlpha, titleFont, playerFont);
				return;
			}
			textureFallback = true;
		}
		computeLayers(nearLattice, threshold(p, nearSettings), layers);
		computeLayers(farLattice, threshold(p, farSettings), farLayers);
		if (!surface || surface->getW() != layers.w || surface->getH() != layers.h)
		{
			surface = std::make_unique<SmoothSurface>(layers.w, layers.h);
		}
		SDL_Surface *pixels = surface->getSDLSurface();
		composite(w, h, p, layers, farLayers, shade, static_cast<Uint32 *>(pixels->pixels), pixels->pitch);
		surface->markPixelsChanged();
		gfx.drawSurface(0, 0, w, h, surface.get());
	}
	if (titleAlpha > 0)
		drawTitle(gfx, w, layout, titleAlpha, titleFont, playerFont);
}

void IntroCover::drawTitle(DrawableSurface &gfx, int w, const TitleLayout &layout, float alpha, Font *titleFont,
                           Font *playerFont)
{
	if (!titleFont || title.map.empty())
		return;
	const Uint8 a = Uint8(std::lround(255 * alpha));
	// Soft white letters: a wide, faint shadow stands in for a blur.
	const auto shadowed = [&](Font *font, int x, int y, const std::string &text, Uint8 textAlpha)
	{
		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 10, 16, 28));
		for (auto [dx, dy] : {std::pair{0, 1}, std::pair{1, 2}, std::pair{-1, 2}, std::pair{0, 3}, std::pair{2, 3},
		                      std::pair{-2, 3}, std::pair{0, 4}})
			gfx.drawString(x + dx, y + dy, font, text, 0, Uint8(textAlpha / 5));
		font->popStyle();
		font->pushStyle(Font::Style(Font::STYLE_NORMAL, 255, 255, 255));
		gfx.drawString(x, y, font, text, 0, textAlpha);
		font->popStyle();
	};
	shadowed(titleFont, (w - layout.titleW) / 2, layout.top, title.map, a);
	if (!playerFont)
		return;
	const int dot = dotSpace(playerFont);
	const int fontH = playerFont->getStringHeight("Ag");
	int y = layout.top + layout.titleH + 14;
	for (size_t r = 0; r < layout.rows.size(); ++r, y += layout.rowH)
	{
		int x = (w - layout.widths[r]) / 2;
		for (const Player *player : layout.rows[r])
		{
			// A small round dot in the team's colour, then the name in white.
			const float radius = dot * .28f, dcx = x + dot * .35f, dcy = y + fontH * .55f;
			for (int dy = -int(radius) - 1; dy <= int(radius) + 1; ++dy)
			{
				const float half = std::sqrt(std::max(0.f, radius * radius - float(dy * dy)));
				if (half > 0)
				{
					gfx.drawFilledRect(int(std::lround(dcx - half)), int(dcy) + dy + 1, int(std::lround(2 * half)), 1, 10, 16,
					                   28, Uint8(a / 3));
					gfx.drawFilledRect(int(std::lround(dcx - half)), int(dcy) + dy, int(std::lround(2 * half)), 1, player->r,
					                   player->g, player->b, a);
				}
			}
			shadowed(playerFont, x + dot, y, player->name, Uint8(a * 4 / 5));
			x += dot + playerFont->getStringWidth(player->name) + layout.gap;
		}
	}
}
