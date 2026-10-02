// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GraphicContext.h>
#include <array>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
namespace GAGCore
{
class GraphicContext;
class MapGeometryCache;
// Vertex payload shared by immediate batching and persistent map geometry.
// Layers refer to immutable array-page slots; UVs retain the source texture's
// sampling and mip levels. Hue and opacity are per vertex, avoiding uniforms
// that would otherwise force a separate submission for each team or fade.
struct QueueVertex
{
	float x = 0, y = 0, u = 0, v = 0, tu = 0, tv = 0, hue = 0, alpha = 1;
	Color color{255, 255, 255, 255};
	float baseLayer = 0, teamLayer = 0;
};
struct QueueKey
{
	// 0 team sprite, 1 fill, 2 original GL_LINES outline, 3 texture,
	// 4 array team sprite, 5 array texture. Primitive types never change.
	enum Kind
	{
		TeamSprite,
		FilledQuad,
		Outline,
		Texture,
		ArrayTeamSprite,
		ArrayTexture
	};
	Kind kind = TeamSprite;
	unsigned base = 0, team = 0;
	bool hasBase = false, hasTeam = false, blend = true;
	float width = 1;
	bool operator==(const QueueKey &) const = default;
};
struct ArrayView
{
	unsigned texture = 0;
	float layer = 0;
};
// Owned by a single desktop GL context and used only on its presentation thread.
// Capacity limits bound CPU work; barriers submit before unsupported draws,
// texture mutation/deletion, and state changes. Software and portable renderers
// never create this object and continue through their ordinary draw paths.
class RenderBatch
{
	struct State;
	std::unique_ptr<State> state;

  public:
	using Capture = std::function<void(const QueueKey &, const std::vector<QueueVertex> &)>;
	explicit RenderBatch(GraphicContext *);
	~RenderBatch();
	RenderBatch(const RenderBatch &) = delete;
	RenderBatch &operator=(const RenderBatch &) = delete;
	void configure(unsigned, int, int, const char *, const char *);
	void begin();
	void end();
	void barrier();
	void stateChange();
	bool active() const;
	bool outside(float, float, float, float) const;
	bool append(QueueKey, const std::array<QueueVertex, 8> &, int);
	void beginCapture(Capture);
	void endCapture();
	void abortCapture() noexcept;
	void bind(const QueueKey &);
	void finishReplay();
	ArrayView pack(unsigned);
	void textureChanged(unsigned);
	uint64_t textureGeneration() const;
	size_t textureBytes() const;
	MapGeometryCache &geometryCache();
};
// A scope encompasses a ground or air pass, preserving painter order with
// bars, icons and overlapping sprites. Unwinding discards pending commands.
class UnitDrawBatch
{
	RenderBatch *batch;
	int exceptions;

  public:
	explicit UnitDrawBatch(GraphicContext *);
	~UnitDrawBatch() noexcept(false);
	UnitDrawBatch(const UnitDrawBatch &) = delete;
	UnitDrawBatch &operator=(const UnitDrawBatch &) = delete;
};
} // namespace GAGCore
