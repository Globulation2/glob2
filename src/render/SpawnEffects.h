// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The effect played when a swarm produces a unit. The swarm glows briefly, the
// unit fades in as a team-colour silhouette that settles over a second, and a
// short cloud of team-coloured dust twinkles on the unit's birth tile.
//
// Render-only: accelerated renderers draw it from GameAnimations spawn records
// carried in the snapshot's effects; the software renderer does not. Ages are in
// simulation ticks (GAME_TICKS_PER_SECOND), fractional between ticks. Distances
// and sizes are map pixels (32 per tile), measured from the centre of the tile
// they belong to.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace SpawnEffect
{
constexpr float EmergeTicks = 10;      //!< the unit fades in and slides out of the swarm
constexpr float ShimmerTicks = 30;     //!< its silhouette settles to normal shading
constexpr float DustTicks = 22.5f;     //!< the dust cloud fades out
constexpr float PulseRiseTicks = 3;    //!< the swarm glow brightens...
constexpr float PulseFallTicks = 10;   //!< ...then fades
constexpr int ShimmerTwinkles = 5;
//! Twinkles start in turn over the shimmer, the last ending a little after it.
constexpr float TwinkleStagger = 0.14f * ShimmerTicks, TwinkleTicks = 0.55f * ShimmerTicks;
constexpr float TwinklesEnd = (ShimmerTwinkles - 1) * TwinkleStagger + TwinkleTicks;
//! Every part has finished by this age, after which the record is dropped.
constexpr int LifetimeTicks = 34;
static_assert(LifetimeTicks >= TwinklesEnd && LifetimeTicks >= DustTicks &&
              LifetimeTicks >= PulseRiseTicks + PulseFallTicks && LifetimeTicks >= EmergeTicks,
              "spawn records must outlive every part of the effect");

//! Scale of the swarm glow, the unit's slide and its emerging tint.
constexpr float PulseStrength = 0.2f;
//! Peak opacity of the swarm's team-colour glow, before PulseStrength.
constexpr float SwarmGlow = 0.5f;
constexpr int DustMotes = 84;
//! Opacity scale of the whole dust cloud, and of its white glints within it.
constexpr float DustIntensity = 0.5f, DustGlint = 0.6f;
//! A sprite's silhouette is laid over it with ordinary blending, so it covers
//! rather than brightens; this keeps some of the sprite showing through.
constexpr float SpriteSilhouetteOpacity = 0.75f;

inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float easeIn(float t) { return t * t; }
inline float easeOut(float t) { return 1 - (1 - t) * (1 - t); }
inline float smoothstep(float t) { return t * t * (3 - 2 * t); }

//! How a newly produced unit is drawn at `age`.
struct UnitLook
{
	float opacity;    //!< multiplies the unit's own opacity
	float slide;      //!< map pixels the unit is drawn back towards the swarm
	float silhouette; //!< opacity of the team-colour silhouette over it
};
inline UnitLook unitLook(float age)
{
	const float t = clamp01(age / EmergeTicks), settle = clamp01(age / ShimmerTicks);
	const float emergeTint = 0.7f * PulseStrength * (1 - t);
	const float shimmer = 0.95f * (1 - smoothstep(settle));
	return {easeOut(t), (1 - easeOut(t)) * 10 * PulseStrength, std::max(emergeTint, shimmer)};
}

//! Opacity of the producing swarm's team-colour glow at `age`.
inline float swarmGlow(float age)
{
	float pulse = 0;
	if (age >= 0 && age < PulseRiseTicks)
		pulse = easeOut(age / PulseRiseTicks);
	else if (age >= PulseRiseTicks && age < PulseRiseTicks + PulseFallTicks)
		pulse = 1 - easeIn((age - PulseRiseTicks) / PulseFallTicks);
	return SwarmGlow * PulseStrength * pulse;
}

//! Small deterministic generator, so a spawn's particles keep their identity
//! from frame to frame without any stored state.
class Random
{
public:
	explicit Random(std::uint32_t seed) : state(seed * 0x9E3779B9u + 0x7F4A7C15u)
	{
		if (!state) state = 1;
		next(); next();
	}
	//! Uniform in [0, 1).
	float next()
	{
		state ^= state << 13; state ^= state >> 17; state ^= state << 5;
		return (state >> 8) * (1.f / 16777216.f);
	}
private:
	std::uint32_t state;
};

//! One sparkle: a team-colour star with a white core, and a larger white star
//! flashing over it while `glint` is above zero.
struct Sparkle
{
	float x, y;  //!< offset from the effect's centre
	float size;  //!< drawn width and height
	float alpha, glint;
};

//! Twinkles drifting up over the unit while its silhouette settles.
template <class Visit>
void forEachShimmerTwinkle(std::uint32_t seed, float age, Visit visit)
{
	Random random(seed ^ 0x51u);
	for (int k = 0; k < ShimmerTwinkles; ++k)
	{
		// Draw every random number before any early exit so each twinkle keeps its identity.
		const float x = (random.next() - 0.5f) * 20;
		const float t = (age - k * TwinkleStagger) / TwinkleTicks;
		if (t <= 0 || t >= 1)
			continue;
		visit(Sparkle{x, lerp(6, -18, t), 7, std::sin(t * 3.14159265f), 0});
	}
}

//! Seeds a spawn's particles from where and what it produced.
inline std::uint32_t seedOf(int x, int y, std::uint16_t unit)
{
	return std::uint32_t(unit) * 2654435761u ^ (std::uint32_t(x) << 16 | std::uint32_t(y & 0xFFFF));
}

//! A soft team-colour glow under the dust cloud.
struct Glow
{
	float x, y, radius, alpha;
	float lightness; //!< HSL lightness of its centre, 0..1
};
template <class Visit>
void forEachDustGlow(float age, Visit visit)
{
	if (age < 0 || age >= DustTicks)
		return;
	const float t = age / DustTicks, appear = clamp01(age / 3);
	visit(Glow{0, -10 * t, lerp(18, 42, easeOut(t)), 0.5f * DustIntensity * appear * (1 - t), 0.82f});
	const float flash = clamp01(std::min(age / 24, age / (DustTicks * 0.6f)));
	visit(Glow{0, 0, 14, 0.7f * DustIntensity * appear * (1 - easeOut(flash)), 0.92f});
}

//! The dust cloud's motes: they spread from the birth tile, rise, sway and
//! twinkle, flashing white now and then, and fade out by DustTicks.
template <class Visit>
void forEachDustMote(std::uint32_t seed, float age, Visit visit)
{
	if (age < 0 || age >= DustTicks)
		return;
	Random random(seed);
	const float appear = clamp01(age / 3);
	for (int k = 0; k < DustMotes; ++k)
	{
		// Draw every random number before any early exit so each mote keeps its identity.
		const float angle = random.next() * 6.2831853f, reach = 12 + random.next() * 36;
		const float life = DustTicks * (0.6f + random.next() * 0.4f), phase = random.next() * 6.2831853f;
		const float size = 4 + random.next() * 6, rise = 10 + random.next() * 26;
		const float sway = 2 + random.next() * 4, rate = 0.18f + random.next() * 0.2f;
		if (age >= life)
			continue;
		const float lt = age / life;
		const float spread = reach * (1 - std::exp(-age / std::min(12.f, DustTicks / 4)));
		const float x = std::cos(angle) * spread + std::sin(age * 0.04f + phase) * sway;
		const float y = std::sin(angle) * spread * 0.7f - rise * easeOut(lt);
		const float fading = DustIntensity * appear * (1 - lt * lt);
		// Each mote flashes white for a few ticks at its own rhythm.
		const float flash = std::pow(std::max(0.f, std::sin(age * rate + phase * 3)), 14.f);
		visit(Sparkle{x, y, size * (1 - 0.4f * lt) * (1 + 0.6f * flash),
			fading * (0.8f + 0.2f * std::sin(age * 0.22f + phase)), fading * flash * DustGlint});
	}
}
}
