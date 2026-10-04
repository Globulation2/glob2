// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

// Touch scroll physics shared by the UI widgets, the in-game viewport and HUD
// panels, and the map editor: release-velocity estimation, momentum
// (deceleration), rubber-band overscroll while dragging past an edge, and a
// critically damped spring back to the edge. Everything is evaluated in closed
// form at an absolute time the caller provides in milliseconds, so the motion is
// identical at any frame rate and deterministic in tests. Nothing here reads a
// clock or touches SDL.
namespace GAGCore
{
using Ticks = std::uint64_t;

struct ScrollPhysicsConfig
{
	// Velocity multiplier per millisecond while coasting: v(t) = v0 * rate^t.
	// 0.998 is the iOS normal deceleration rate (time constant ~500 ms).
	double decelerationRate = 0.998;
	// Rubber band f(x) = x*d*c / (d + c*x) with d the viewport extent; 0.55 is iOS.
	double rubberBandCoefficient = 0.55;
	// Spring-back response in ms; lambda = 2*pi/response, critically damped.
	double springResponse = 500;
	// Distances are in the caller's scroll units; velocities are units per ms.
	double minFlingVelocity = 0.05;  // below this a release does not coast (50 units/s)
	double maxFlingVelocity = 8.0;   // release velocity clamp (8000 units/s)
	double maxBounceVelocity = 2.0;  // velocity handed to the spring at an edge
	double stopVelocity = 0.002;     // coasting ends below this (residual < 1 unit)
	double settleDistance = 0.5;     // spring ends inside this distance of the edge
	bool momentum = true;            // coast after release
	bool bounce = true;              // rubber band and spring at the edges (else clamp)
	bool wrap = false;               // toroidal: no bounds, no rubber band, no bounce
	// The same behaviour in units scaled by `unit` (points to pixels, say).
	ScrollPhysicsConfig scaled(double unit) const;
};

// Player-facing tuning, 0..100 per slider: 0 turns the effect off, 50 is the
// research default. Written by the application from its preferences; libgag has
// no access to Settings. Presets read it every time they are built.
struct ScrollTuning
{
	int momentum = 50;      // lists, panels, trays
	int bounce = 50;        // lists, panels, trays
	int mapMomentum = 50;   // game and editor viewports
	bool reducedMotion = false; // decorative bounce off
};
ScrollTuning &scrollTuning();

// Slider level to deceleration rate: 50 -> 0.998, 100 -> 0.9995, 1 -> ~0.992.
double decelerationRateForLevel(int level);
// Slider level to rubber band coefficient: 50 -> 0.55, linear.
double rubberBandForLevel(int level);

namespace ScrollPresets
{
ScrollPhysicsConfig widget();       // libgag scroll, list and text nodes (touch)
ScrollPhysicsConfig mouse();        // any surface driven by a mouse: drag only
ScrollPhysicsConfig hudPanel();     // in-game palette, inspector, tutorial, editor inspector
ScrollPhysicsConfig mapViewport();  // toroidal map: momentum, no edges
ScrollPhysicsConfig editorTray();   // horizontal editor tool strip
} // namespace ScrollPresets

// SDL event timestamps are 32-bit SDL_GetTicks values; the frame clock may be
// 64-bit. Widen `stamp` to the 2^32 period nearest `reference`.
Ticks widenTicks(std::uint32_t stamp, Ticks reference);

// Release velocity from recent (time, position) samples: a second-order least
// squares fit over the last `window` ms, evaluated at the query time. Two
// samples fit a line; one, none, a zero time span or a newest sample older than
// `stale` ms (the finger paused before lifting) give zero.
class VelocityTracker
{
  public:
	static constexpr Ticks window = 100, stale = 40;
	void reset();
	void add(Ticks t, double position);
	double velocity(Ticks t) const;
	std::size_t size() const { return count; }

  private:
	struct Sample
	{
		Ticks t = 0;
		double x = 0;
	};
	std::array<Sample, 24> ring{};
	std::size_t head = 0, count = 0;
};

// One scroll axis. Offsets are in scroll units, unbounded in wrap mode and
// otherwise usually within [minimum, maximum]; while dragging past an edge or
// bouncing back the offset lies outside and overscroll() is the excess.
class ScrollAxis
{
  public:
	enum class Phase
	{
		Idle,
		Dragging,
		Decelerating,
		Bouncing
	};
	explicit ScrollAxis(const ScrollPhysicsConfig &config = {});
	void setConfig(const ScrollPhysicsConfig &config);
	const ScrollPhysicsConfig &config() const { return cfg; }

	// Content range and the viewport extent along this axis (the rubber band's
	// reach). Ignored in wrap mode. Safe to call every frame.
	void setBounds(double minimum, double maximum, double extent);
	double minimum() const { return low; }
	double maximum() const { return high; }
	// External jump (wheel, scrollbar, programmatic): stops any motion.
	void setOffset(double offset);
	double offset() const { return value; }
	double clampedOffset() const;
	double overscroll() const { return value - clampedOffset(); }
	Phase phase() const { return state; }
	bool isAnimating() const { return state == Phase::Decelerating || state == Phase::Bouncing; }
	bool isDragging() const { return state == Phase::Dragging; }

	// A finger landed: anchor at the current offset (also while coasting or
	// bouncing, which stop). In wrap mode the offset restarts at zero.
	void beginDrag(Ticks t);
	// The finger moved the content by `delta` units.
	void drag(Ticks t, double delta);
	// The finger lifted: coast, spring back, or stop.
	void endDrag(Ticks t);
	// A finger landed but never dragged: spring back if overscrolled, else nothing.
	void settle(Ticks t);
	// Stop coasting or bouncing where it is. A stopped bounce keeps its
	// stretch (a finger is holding it) until the next drag, settle or jump.
	void interrupt();
	// Advance to absolute time `t` and return the offset.
	double step(Ticks t);
	// Where coasting will end (clamped unless wrapping); the offset otherwise.
	double projectedEnd() const;

  private:
	double rubber(double excess) const;
	double unrubber(double stretched) const;
	double lambda() const;
	void startBounce(Ticks t, double edge, double overshoot, double velocity);
	ScrollPhysicsConfig cfg;
	Phase state = Phase::Idle;
	bool held = false; // idle but stretched: a stopped bounce under a finger
	double low = 0, high = 0, extent = 1, value = 0;
	double anchor = 0, raw = 0;       // drag origin and finger displacement since
	Ticks t0 = 0;                     // start of the coast or bounce
	double x0 = 0, v0 = 0;            // coast start offset and velocity
	double edge = 0, c1 = 0, c2 = 0;  // spring: x = edge + (c1 + c2*tau)*e^(-lambda*tau)
	VelocityTracker tracker;
};

// An axis bound to a plain offset variable that other code also assigns
// (wheel steps, page keys, resets). sync() adopts such an assignment as an
// external jump, refreshes the bounds and copies the axis back, so the
// variable always holds what the axis says; publish() copies after a frame.
class TrackedScrollAxis
{
  public:
	ScrollAxis axis;
	bool externallyMoved(double value) const { return value != written; }
	void sync(double &value, double maximum, double extent)
	{
		if (value != written)
			axis.setOffset(value);
		axis.setBounds(0, maximum, extent);
		publish(value);
	}
	void publish(double &value) { value = written = axis.offset(); }

  private:
	double written = 0;
};

// Two axes driven by one gesture; consumers that wrap read per-step deltas.
struct ScrollMotion
{
	ScrollAxis x, y;
	explicit ScrollMotion(const ScrollPhysicsConfig &config = {}) : x(config), y(config) {}
	void setConfig(const ScrollPhysicsConfig &config);
	void beginDrag(Ticks t);
	void drag(Ticks t, double dx, double dy);
	void endDrag(Ticks t);
	void interrupt();
	bool isAnimating() const { return x.isAnimating() || y.isAnimating(); }
	bool isDragging() const { return x.isDragging() || y.isDragging(); }
	// Offset change on each axis since the previous step.
	std::pair<double, double> stepDelta(Ticks t);
};
} // namespace GAGCore
