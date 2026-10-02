// SPDX-License-Identifier: GPL-3.0-or-later
#include "GraphicContextPrivate.h"
#include <RenderBatch.h>
#include <MapGeometryCache.h>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <map>
#include <tuple>
#include <unordered_map>
#include <exception>
#include <cstring>
#include <stdexcept>
namespace GAGCore
{
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
namespace
{
struct Bounds
{
	float l, t, r, b;
	bool overlaps(const Bounds &o) const { return l < o.r && r > o.l && t < o.b && b > o.t; }
	void add(const Bounds &o)
	{
		l = std::min(l, o.l);
		t = std::min(t, o.t);
		r = std::max(r, o.r);
		b = std::max(b, o.b);
	}
};
struct Command
{
	std::array<QueueVertex, 8> v;
	int n, next = -1;
};
struct Run
{
	QueueKey key;
	Bounds bounds;
	int first, last;
};

} // namespace
struct RenderBatch::State
{
	GraphicContext *gfx;
	std::vector<Command> commands;
	std::vector<Run> runs;
	std::vector<QueueVertex> vertices;
	bool active = false, cull = false, reorder = true;
	float pixelWorld = 1, margin = .5;
	Bounds clip{};
	unsigned queueProgram = 0, arrayUnitProgram = 0, arrayPlainProgram = 0;
	int queueHasBase = -1, queueHasTeam = -1, arrayHasBase = -1, arrayHasTeam = -1;
	using ArrayDescriptor = std::tuple<int, int, int, int, int, int, int, int>;
	struct Page
	{
		unsigned texture;
		int used = 0;
		size_t bytes;
		int layers;
	};
	std::map<ArrayDescriptor, std::vector<Page>> pages;
	std::unordered_map<unsigned, ArrayView> views;
	size_t bytes = 0;
	int maxLayers = 0;
	uint64_t generation = 0;
	Capture capture;
	std::unique_ptr<MapGeometryCache> geometry;
	explicit State(GraphicContext *g) : gfx(g) {}
	void flush()
	{
		using namespace GAGCore;
		vertices.clear();
		vertices.reserve(4096 * 8);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glEnableClientState(GL_VERTEX_ARRAY);
		unsigned boundProgram = ~0u, boundTeam = ~0u;
		int hasBase = -1, hasTeam = -1, attributeKind = -1;
		for (const auto &run : runs)
		{
			auto k = run.key;
			glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glState.doBlend(k.blend);
			const unsigned program = (k.kind == QueueKey::ArrayTeamSprite ? arrayUnitProgram
									  : k.kind == QueueKey::ArrayTexture  ? arrayPlainProgram
									  : k.kind == QueueKey::TeamSprite    ? queueProgram
																		  : 0);
			if (program != boundProgram)
			{
				glUseProgram(program);
				boundProgram = program;
				hasBase = hasTeam = -1;
			}
			if (k.kind == QueueKey::ArrayTeamSprite || k.kind == QueueKey::ArrayTexture)
			{
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D_ARRAY_EXT, k.base);
				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D_ARRAY_EXT, k.team);
				glActiveTexture(GL_TEXTURE0);
				if (k.kind == QueueKey::ArrayTeamSprite)
				{
					if (hasBase != k.hasBase)
					{
						glUniform1i(arrayHasBase, k.hasBase);
						hasBase = k.hasBase;
					}
					if (hasTeam != k.hasTeam)
					{
						glUniform1i(arrayHasTeam, k.hasTeam);
						hasTeam = k.hasTeam;
					}
				}
			}
			else if (k.kind == QueueKey::TeamSprite)
			{
				glState.setTexture(k.base);
				if (boundTeam != k.team)
				{
					glActiveTexture(GL_TEXTURE1);
					glBindTexture(GL_TEXTURE_2D, k.team);
					glActiveTexture(GL_TEXTURE0);
					boundTeam = k.team;
				}
				if (hasBase != k.hasBase)
				{
					glUniform1i(queueHasBase, k.hasBase);
					hasBase = k.hasBase;
				}
				if (hasTeam != k.hasTeam)
				{
					glUniform1i(queueHasTeam, k.hasTeam);
					hasTeam = k.hasTeam;
				}
			}
			else
			{
				glState.doTexture(k.kind == QueueKey::Texture);
				if (k.kind == QueueKey::Texture)
					glState.setTexture(k.base);
				if (k.kind == QueueKey::Outline)
					glLineWidth(k.width);
			}
			vertices.clear();
			for (int i = run.first; i != -1; i = commands[i].next)
			{
				auto &cmd = commands[i];
				vertices.insert(vertices.end(), cmd.v.begin(), cmd.v.begin() + cmd.n);
			}
			if (capture)
			{
				capture(k, vertices);
				continue;
			}
			const auto *v = vertices.data();
			glVertexPointer(2, GL_FLOAT, sizeof(QueueVertex), &v->x);
			int layout = (k.kind == QueueKey::TeamSprite || k.kind == QueueKey::ArrayTeamSprite ||
						  k.kind == QueueKey::ArrayTexture)
							 ? 0
						 : k.kind == QueueKey::Texture ? 3
													   : 1;
			if (layout != attributeKind)
			{
				for (int unit = 0; unit < 4; ++unit)
				{
					glClientActiveTexture(GL_TEXTURE0 + unit);
					if (layout == 0 || (layout == 3 && unit == 0))
						glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					else
						glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				}
				glClientActiveTexture(GL_TEXTURE0);
				if (layout == 0)
					glDisableClientState(GL_COLOR_ARRAY);
				else
					glEnableClientState(GL_COLOR_ARRAY);
				attributeKind = layout;
			}
			if (layout == 0 || layout == 3)
			{
				glClientActiveTexture(GL_TEXTURE0);
				glTexCoordPointer(2, GL_FLOAT, sizeof(QueueVertex), &v->u);
			}
			if (layout == 0)
			{
				glClientActiveTexture(GL_TEXTURE3);
				glTexCoordPointer(2, GL_FLOAT, sizeof(QueueVertex), &v->baseLayer);
				glClientActiveTexture(GL_TEXTURE1);
				glTexCoordPointer(2, GL_FLOAT, sizeof(QueueVertex), &v->tu);
				glClientActiveTexture(GL_TEXTURE2);
				glTexCoordPointer(2, GL_FLOAT, sizeof(QueueVertex), &v->hue);
				glClientActiveTexture(GL_TEXTURE0);
			}
			if (layout != 0)
				glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(QueueVertex), &v->color.r);
			glDrawArrays(k.kind == QueueKey::Outline ? GL_LINES : GL_QUADS, 0,
						 int(vertices.size()));
			gfx->countRenderBatchDraw();
		}
		for (int unit = 0; unit < 4; ++unit)
		{
			glClientActiveTexture(GL_TEXTURE0 + unit);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		}
		glClientActiveTexture(GL_TEXTURE0);
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);
		if (arrayPlainProgram)
		{
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D_ARRAY_EXT, 0);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D_ARRAY_EXT, 0);
		}
		glUseProgram(0);
		commands.clear();
		runs.clear();
	}
	bool isActive() { return active; }
	void barrier()
	{
		if (!commands.empty())
			flush();
	}
	void stateChange()
	{
		barrier();
		cull = false;
		reorder = false;
	}
	void begin()
	{
		if (active)
			throw std::logic_error("Unit draw batch scopes cannot nest");
		commands.reserve(4096);
		runs.reserve(128);
		float m[16], p[16];
		int vp[4], sc[4];
		glGetFloatv(GL_MODELVIEW_MATRIX, m);
		glGetFloatv(GL_PROJECTION_MATRIX, p);
		glGetIntegerv(GL_VIEWPORT, vp);
		glGetIntegerv(GL_SCISSOR_BOX, sc);
		// Convert physical raster coverage to world coordinates once per scope.
		pixelWorld = 1.f / (std::abs(m[0]) * gfx->getRasterScale());
		margin = .5f * pixelWorld;
		auto x = [&](float s)
		{ return (((s - vp[0]) * 2 / vp[2] - 1 - p[12]) / p[0] - m[12]) / m[0]; };
		auto y = [&](float s)
		{ return (((s - vp[1]) * 2 / vp[3] - 1 - p[13]) / p[5] - m[13]) / m[5]; };
		if (!glIsEnabled(GL_SCISSOR_TEST))
			std::copy(vp, vp + 4, sc);
		float x0 = x(sc[0]), x1 = x(sc[0] + sc[2]), y0 = y(sc[1]), y1 = y(sc[1] + sc[3]);
		clip = {std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)};
		cull = true;
		reorder = true;
		active = true;
	}
	void end()
	{
		barrier();
		active = false;
	}
	bool outside(float x, float y, float w, float h)
	{
		if (!active || !cull)
			return false;
		Bounds b{std::min(x, x + w) - margin, std::min(y, y + h) - margin,
				 std::max(x, x + w) + margin, std::max(y, y + h) + margin};
		return !b.overlaps(clip);
	}
	bool append(QueueKey key, const std::array<QueueVertex, 8> &v, int n)
	{
		assert(active);
		Bounds b{v[0].x, v[0].y, v[0].x, v[0].y};
		for (int j = 1; j < n; ++j)
			b.add({v[j].x, v[j].y, v[j].x, v[j].y});
		const float padding =
			margin +
			(key.kind == QueueKey::Outline ? std::max(key.width, 1.f) * pixelWorld / 2 : 0);
		b.l -= padding;
		b.t -= padding;
		b.r += padding;
		b.b += padding;
		if (cull && !b.overlaps(clip))
			return false;
		if (commands.size() == 4096)
			flush();
		// A command may join an earlier matching run only while every intervening
		// run is disjoint. Bounds include half a physical pixel and the requested
		// line half-width; moving across an overlapping run changes alpha compositing.
		// Union bounds deliberately overestimate coverage, making the search safe.
		int selected = -1;
		for (int i = int(runs.size()) - 1; i >= 0; --i)
		{
			if (runs[i].key == key)
			{
				selected = i;
				break;
			}
			if (!reorder || runs[i].bounds.overlaps(b))
				break;
		}
		bool newRun = selected == -1;
		if (newRun)
		{
			if (runs.size() == 128)
				flush();
			selected = int(runs.size());
			runs.push_back({key, b, -1, -1});
		}
		int index = int(commands.size());
		commands.push_back({v, n, -1});
		auto &r = runs[selected];
		if (r.last != -1)
			commands[r.last].next = index;
		else
			r.first = index;
		r.last = index;
		r.bounds.add(b);
		return newRun;
	}

	// Array slots are immutable. Mutation removes only the source-ID mapping;
	// previously captured slots stay alive until context teardown. This avoids
	// dangling page references during capture, and bounds wasted slots by the
	// same 64 MiB cap. New textures fall back to their ordinary 2D textures once
	// the cap is reached. Geometry validates the generation before replay.
	void clearPages()
	{
		if (geometry)
			geometry->clear();
		for (auto &[key, list] : pages)
			for (auto &p : list)
			{
				glDeleteTextures(1, &p.texture);
				--glState.allocatedTextureCount;
				glState.allocatedTextureBytes -= p.bytes;
			}
		pages.clear();
		views.clear();
		bytes = 0;
	}
	~State()
	{
		assert(!active);
		clearPages();
		if (arrayUnitProgram)
			glDeleteProgram(arrayUnitProgram);
		if (arrayPlainProgram)
			glDeleteProgram(arrayPlainProgram);
	}
};
ArrayView RenderBatch::pack(unsigned texture)
{
	auto &s = *state;
	if (!texture || !s.arrayUnitProgram || !s.arrayPlainProgram)
		return {};
	if (auto i = s.views.find(texture); i != s.views.end())
		return i->second;
	barrier();
	// Remember stable ineligibility and budget/driver failures. Otherwise a
	// full page budget would force expensive GPU queries for every sprite.
	// Texture mutation/deletion erases these negative entries as well.
	auto fallback = [&]
	{
		s.views.emplace(texture, ArrayView{});
		return ArrayView{};
	};
	try
	{
		GAGCore::glState.setTexture(texture);
		int w, h, fmt, maxLevel, minFilter, magFilter, ws, wt;
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &maxLevel);
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magFilter);
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &ws);
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &wt);
		// Exact size, format, mip count and sampling form the page key. Copying
		// existing GPU mip data preserves alpha-aware downsampling and compression;
		// this desktop-only readback is amortized over the texture lifetime.
		if (w > 512 || h > 512 || w < 1 || h < 1)
			return fallback();
		if (minFilter == GL_NEAREST || minFilter == GL_LINEAR)
			maxLevel = 0;
		else
			maxLevel = std::min(maxLevel, int(std::floor(std::log2(std::max(w, h)))));
		State::ArrayDescriptor desc{w, h, fmt, maxLevel, minFilter, magFilter, ws, wt};
		auto &pages = s.pages[desc];
		const int layers = std::min(s.maxLayers, (w == 128 && h == 128) ? 256 : 64);
		if (layers < 1)
			return fallback();
		if (pages.empty() || pages.back().used >= layers)
		{
			size_t bytes = 0;
			for (int l = 0; l <= maxLevel; ++l)
			{
				int compressed = 0, n = 0;
				glGetTexLevelParameteriv(GL_TEXTURE_2D, l, GL_TEXTURE_COMPRESSED, &compressed);
				if (compressed)
					glGetTexLevelParameteriv(GL_TEXTURE_2D, l, GL_TEXTURE_COMPRESSED_IMAGE_SIZE,
											 &n);
				else
					n = std::max(1, w >> l) * std::max(1, h >> l) * 4;
				bytes += n * layers;
			}
			if (s.bytes + bytes > 64 * 1024 * 1024)
				return fallback();
			// Reserve metadata before acquiring a GL handle, so bad_alloc cannot
			// leak an untracked page or bypass the GPU memory limit.
			pages.reserve(pages.size() + 1);
			unsigned tex;
			glGenTextures(1, &tex);
			glBindTexture(GL_TEXTURE_2D_ARRAY_EXT, tex);
			glTexParameteri(GL_TEXTURE_2D_ARRAY_EXT, GL_TEXTURE_MIN_FILTER, minFilter);
			glTexParameteri(GL_TEXTURE_2D_ARRAY_EXT, GL_TEXTURE_MAG_FILTER, magFilter);
			glTexParameteri(GL_TEXTURE_2D_ARRAY_EXT, GL_TEXTURE_WRAP_S, ws);
			glTexParameteri(GL_TEXTURE_2D_ARRAY_EXT, GL_TEXTURE_WRAP_T, wt);
			glTexParameteri(GL_TEXTURE_2D_ARRAY_EXT, GL_TEXTURE_MAX_LEVEL, maxLevel);
			for (int l = 0; l <= maxLevel; ++l)
				glTexImage3D(GL_TEXTURE_2D_ARRAY_EXT, l, fmt, std::max(1, w >> l),
							 std::max(1, h >> l), layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			if (glGetError() != GL_NO_ERROR)
			{
				glDeleteTextures(1, &tex);
				glBindTexture(GL_TEXTURE_2D_ARRAY_EXT, 0);
				return fallback();
			}
			pages.push_back({tex, 0, bytes, layers});
			s.bytes += bytes;
			++GAGCore::glState.allocatedTextureCount;
			GAGCore::glState.allocatedTextureBytes += bytes;
		}
		auto &page = pages.back();
		glBindTexture(GL_TEXTURE_2D_ARRAY_EXT, page.texture);
		for (int l = 0; l <= maxLevel; ++l)
		{
			int compressed = 0, n = 0;
			glGetTexLevelParameteriv(GL_TEXTURE_2D, l, GL_TEXTURE_COMPRESSED, &compressed);
			int lw = std::max(1, w >> l), lh = std::max(1, h >> l);
			if (compressed)
			{
				glGetTexLevelParameteriv(GL_TEXTURE_2D, l, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &n);
				std::vector<unsigned char> data(n);
				glGetCompressedTexImage(GL_TEXTURE_2D, l, data.data());
				glCompressedTexSubImage3D(GL_TEXTURE_2D_ARRAY_EXT, l, 0, 0, page.used, lw, lh, 1,
										  fmt, n, data.data());
			}
			else
			{
				std::vector<unsigned char> data(lw * lh * 4);
				glGetTexImage(GL_TEXTURE_2D, l, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
				glTexSubImage3D(GL_TEXTURE_2D_ARRAY_EXT, l, 0, 0, page.used, lw, lh, 1, GL_RGBA,
								GL_UNSIGNED_BYTE, data.data());
			}
		}
		glBindTexture(GL_TEXTURE_2D_ARRAY_EXT, 0);
		if (glGetError() != GL_NO_ERROR)
			return fallback();
		ArrayView v{page.texture, float(page.used++)};
		s.views.emplace(texture, v);
		return v;
	}
	catch (const std::bad_alloc &)
	{
		// Existing pages/slots remain owned. A partially copied unused slot may
		// be overwritten later; no mapping was published, so ordinary 2D drawing
		// remains a safe fallback for this command.
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D_ARRAY_EXT, 0);
		return {};
	}
}

namespace
{
unsigned makeProgram(const std::string &vs, const std::string &fs)
{
	unsigned v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER), p = 0;
	const char *a = vs.c_str(), *b = fs.c_str();
	glShaderSource(v, 1, &a, nullptr);
	glShaderSource(f, 1, &b, nullptr);
	glCompileShader(v);
	glCompileShader(f);
	int vok = 0, fok = 0;
	glGetShaderiv(v, GL_COMPILE_STATUS, &vok);
	glGetShaderiv(f, GL_COMPILE_STATUS, &fok);
	if (vok && fok)
	{
		p = glCreateProgram();
		glAttachShader(p, v);
		glAttachShader(p, f);
		glLinkProgram(p);
		int ok = 0;
		glGetProgramiv(p, GL_LINK_STATUS, &ok);
		if (!ok)
		{
			glDeleteProgram(p);
			p = 0;
		}
	}
	glDeleteShader(v);
	glDeleteShader(f);
	if (p)
	{
		glUseProgram(p);
		glUniform1i(glGetUniformLocation(p, "uBase"), 0);
		int t = glGetUniformLocation(p, "uTeam");
		if (t >= 0)
			glUniform1i(t, 1);
		glUseProgram(0);
	}
	return p;
}
void replaceAll(std::string &s, const std::string &a, const std::string &b)
{
	size_t i = 0;
	while ((i = s.find(a, i)) != std::string::npos)
	{
		s.replace(i, a.size(), b);
		i += b.size();
	}
}
} // namespace
void RenderBatch::configure(unsigned p, int b, int t, const char *vertex, const char *fragment)
{
	auto &s = *state;
	s.queueProgram = p;
	s.queueHasBase = b;
	s.queueHasTeam = t;
	const char *extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
	if (!extensions || !std::strstr(extensions, "GL_EXT_texture_array"))
		return;
	glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS_EXT, &s.maxLayers);
	if (s.maxLayers < 1)
		return;
	std::string vs = vertex, fs = fragment;
	replaceAll(vs, "varying vec2 vHueAlpha;", "varying vec2 vLayers;\nvarying vec2 vHueAlpha;");
	replaceAll(vs, "vHueAlpha =", "vLayers = gl_MultiTexCoord3.st;\nvHueAlpha =");
	replaceAll(fs, "#version 120\n", "#version 120\n#extension GL_EXT_texture_array : require\n");
	replaceAll(fs, "varying vec2 vHueAlpha;", "varying vec2 vLayers;\nvarying vec2 vHueAlpha;");
	replaceAll(fs, "sampler2D ", "sampler2DArray ");
	replaceAll(fs, "texture2D(uBase, vBaseUV)", "texture2DArray(uBase, vec3(vBaseUV,vLayers.x))");
	replaceAll(fs, "texture2D(uTeam, vTeamUV)", "texture2DArray(uTeam, vec3(vTeamUV,vLayers.y))");
	s.arrayUnitProgram = makeProgram(vs, fs);
	s.arrayPlainProgram = makeProgram(
		vs,
		"#version 120\n#extension GL_EXT_texture_array : require\nuniform sampler2DArray "
		"uBase;varying vec2 vBaseUV;varying vec2 vLayers;varying vec2 vHueAlpha;void main(){vec4 "
		"c=texture2DArray(uBase,vec3(vBaseUV,vLayers.x));gl_FragColor=vec4(c.rgb,c.a*vHueAlpha.y);"
		"}");
	if (!s.arrayUnitProgram || !s.arrayPlainProgram)
	{
		if (s.arrayUnitProgram)
			glDeleteProgram(s.arrayUnitProgram);
		if (s.arrayPlainProgram)
			glDeleteProgram(s.arrayPlainProgram);
		s.arrayUnitProgram = s.arrayPlainProgram = 0;
		return;
	}
	s.arrayHasBase = glGetUniformLocation(s.arrayUnitProgram, "uHasBase");
	s.arrayHasTeam = glGetUniformLocation(s.arrayUnitProgram, "uHasTeam");
}
RenderBatch::RenderBatch(GraphicContext *g) : state(std::make_unique<State>(g)) {}
RenderBatch::~RenderBatch() = default;
void RenderBatch::begin()
{
	Sprite::flushBatches(state->gfx);
	state->begin();
}
void RenderBatch::end()
{
	state->end();
}
void RenderBatch::barrier()
{
	state->barrier();
}
void RenderBatch::stateChange()
{
    // Transform scopes restore state while unwinding. Discard a failed pass
    // before that restore, rather than risk a second allocation exception.
    if (std::uncaught_exceptions() > 0) { abortCapture(); return; }
	state->stateChange();
}
bool RenderBatch::active() const
{
	return state->active;
}
bool RenderBatch::outside(float x, float y, float w, float h) const
{
	return state->active && state->outside(x, y, w, h);
}
bool RenderBatch::append(QueueKey k, const std::array<QueueVertex, 8> &v, int n)
{
	state->append(k, v, n);
	return true;
}
void RenderBatch::beginCapture(Capture c)
{
	begin();
	state->capture = std::move(c);
	state->cull = false;
	state->margin = 2 * state->pixelWorld;
}
void RenderBatch::endCapture()
{
	end();
	state->capture = {};
}
void RenderBatch::abortCapture() noexcept
{
	state->commands.clear();
	state->runs.clear();
	state->capture = {};
	state->active = false;
	finishReplay();
}
void RenderBatch::textureChanged(unsigned texture)
{
	if (!texture)
		return;
	barrier();
	if (state->geometry)
		state->geometry->clear();
	++state->generation;
	state->views.erase(texture);
}
uint64_t RenderBatch::textureGeneration() const
{
	return state->generation;
}
size_t RenderBatch::textureBytes() const
{
	return state->bytes;
}
MapGeometryCache &RenderBatch::geometryCache()
{
	if (!state->geometry)
		state->geometry = std::make_unique<MapGeometryCache>(state->gfx, this);
	return *state->geometry;
}
void RenderBatch::bind(const QueueKey &k)
{
	auto &s = *state;
	glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glState.doBlend(k.blend);
	glState.doTexture(k.kind == QueueKey::Texture);
	glUseProgram(k.kind == QueueKey::ArrayTexture ? s.arrayPlainProgram : 0);
	if (k.kind == QueueKey::ArrayTexture)
		glBindTexture(GL_TEXTURE_2D_ARRAY_EXT, k.base);
	else
		glState.setTexture(k.base);
}
void RenderBatch::finishReplay()
{
	for (int unit = 0; unit < 4; ++unit)
	{
		glClientActiveTexture(GL_TEXTURE0 + unit);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}
	glClientActiveTexture(GL_TEXTURE0);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glActiveTexture(GL_TEXTURE0);
	glUseProgram(0);
	if (state->arrayPlainProgram)
		glBindTexture(GL_TEXTURE_2D_ARRAY_EXT, 0);
}
#else
struct RenderBatch::State
{
};
RenderBatch::RenderBatch(GraphicContext *) : state(std::make_unique<State>()) {}
RenderBatch::~RenderBatch() = default;
void RenderBatch::configure(unsigned, int, int, const char *, const char *) {}
void RenderBatch::begin() {}
void RenderBatch::end() {}
void RenderBatch::barrier() {}
void RenderBatch::stateChange() {}
bool RenderBatch::active() const
{
	return false;
}
bool RenderBatch::outside(float, float, float, float) const
{
	return false;
}
bool RenderBatch::append(QueueKey, const std::array<QueueVertex, 8> &, int)
{
	return false;
}
void RenderBatch::beginCapture(Capture) {}
void RenderBatch::endCapture() {}
void RenderBatch::abortCapture() noexcept {}
void RenderBatch::textureChanged(unsigned) {}
uint64_t RenderBatch::textureGeneration() const
{
	return 0;
}
size_t RenderBatch::textureBytes() const
{
	return 0;
}
ArrayView RenderBatch::pack(unsigned)
{
	return {};
}
void RenderBatch::bind(const QueueKey &) {}
void RenderBatch::finishReplay() {}
MapGeometryCache &RenderBatch::geometryCache()
{
	throw std::logic_error("Map geometry requires desktop GL");
}
#endif
void GraphicContext::setRenderBatchEnabled(bool enabled)
{
	if (renderBatch)
		renderBatch->barrier();
	renderBatchEnabled = enabled;
}
UnitDrawBatch::UnitDrawBatch(GraphicContext *g)
	: batch(g->getRenderBatch()), exceptions(std::uncaught_exceptions())
{
	if (batch)
		batch->begin();
}
UnitDrawBatch::~UnitDrawBatch() noexcept(false)
{
	if (!batch)
		return;
	if (std::uncaught_exceptions() > exceptions)
		batch->abortCapture();
	else
	{
		try
		{
			batch->end();
		}
		catch (...)
		{
			batch->abortCapture();
			throw;
		}
	}
}
} // namespace GAGCore
