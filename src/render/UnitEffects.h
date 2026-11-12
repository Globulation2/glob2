// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Effects played when a building changes a unit. When a swarm produces a unit,
// the swarm glows briefly, the unit fades in as a team-colour silhouette that
// settles over a second and a half, and a short cloud of team-coloured dust twinkles on its
// birth tile. When a unit leaves a building that healed, fed or trained it, it
// shimmers in the service's colour while copies of that building's map icon
// burst out of it; a trained unit also floats its new level. When a hungry unit
// defects to the team whose inn will feed it, it shimmers in its new colour, and
// what else plays depends on who watches (ConvertView): its new colours gather
// onto it and a heart rises, or its old colours fall off and a heart breaks.
//
// Render-only: accelerated renderers draw them from GameAnimations records
// carried in the snapshot's effects; the software renderer does not. Ages are in
// simulation ticks (GAME_TICKS_PER_SECOND), fractional between ticks. Distances
// and sizes are map pixels (32 per tile), measured from the centre of the tile
// they belong to.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace UnitEffect
{
//! What the building did; stored in snapshot records as its value.
enum class Kind : std::uint8_t { Spawn, Heal, Feed, Train, Convert };

constexpr float EmergeTicks = 15;      //!< the unit fades in and slides out of the swarm
constexpr float ShimmerTicks = 45;     //!< its silhouette settles to normal shading
constexpr float DustTicks = 33.75f;     //!< the dust cloud fades out
constexpr float PulseRiseTicks = 4.5f;    //!< the swarm glow brightens...
constexpr float PulseFallTicks = 15;   //!< ...then fades
constexpr int ShimmerTwinkles = 5;
//! Twinkles start in turn over the shimmer, the last ending a little after it.
constexpr float TwinkleStagger = 0.14f * ShimmerTicks, TwinkleTicks = 0.55f * ShimmerTicks;
constexpr float TwinklesEnd = (ShimmerTwinkles - 1) * TwinkleStagger + TwinkleTicks;
//! Every part has finished by this age, after which the record is dropped.
constexpr int LifetimeTicks = 51;
//! A serviced unit's silhouette, in the service's colour, settles like a spawn's.
constexpr float ServiceShimmerTicks = 45, ServiceShimmerPeak = 0.9f;
constexpr int ServiceIcons = 1;
//! Icons start up to IconDelayTicks late and each lasts up to IconTicks.
constexpr float IconTicks = 33.75f, IconDelayTicks = 6;
constexpr float IconSize = 12, IconReach = 30;
//! A trained unit's level number rises like a combat level-up's.
constexpr float LevelTicks = 30;
//! A service effect waits for its unit to be half out of the building, at most this long.
constexpr int MaxEmergeTicks = 16;
//! Ticks a unit moving `speed` (action delta per tick) takes to be half out.
inline int emergeTicks(int speed) { return speed > 0 ? std::min(MaxEmergeTicks, 128 / speed) : 0; }
// Service records age from -emergeTicks, so a wait does not shorten their effect.
static_assert(LifetimeTicks >= ServiceShimmerTicks && LifetimeTicks >= IconDelayTicks + IconTicks &&
              LifetimeTicks >= LevelTicks, "service records must outlive their effect");
static_assert(LifetimeTicks >= TwinklesEnd && LifetimeTicks >= DustTicks &&
              LifetimeTicks >= PulseRiseTicks + PulseFallTicks && LifetimeTicks >= EmergeTicks,
              "spawn records must outlive every part of the effect");

//! A converted unit: its new colours gather onto it, then its silhouette settles.
constexpr float GatherTicks = 20, ConvertShimmerTicks = 30, ConvertShimmerPeak = 0.85f;
//! The old colours fall off it as motes.
constexpr float ShedTicks = 30;
constexpr int ShedMotes = 28;
//! A heart rises over the unit (gained), or breaks in two and falls (lost).
constexpr float HeartTicks = 30, GainedHeartStart = GatherTicks - 3, LostHeartStart = 3;
static_assert(LifetimeTicks >= GatherTicks + ConvertShimmerTicks && LifetimeTicks >= ShedTicks &&
              LifetimeTicks >= GainedHeartStart + HeartTicks && LifetimeTicks >= LostHeartStart + HeartTicks,
              "conversion records must outlive their effect");

//! Scale of the swarm glow, the unit's slide and its emerging tint.
constexpr float PulseStrength = 0.2f;
//! Peak opacity of the swarm's team-colour glow, before PulseStrength.
constexpr float SwarmGlow = 0.5f;
constexpr int DustMotes = 84;
//! Opacity scale of the whole dust cloud, and of its white glints within it.
constexpr float DustIntensity = 0.5f, DustGlint = 0.6f;

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

//! Opacity of a serviced unit's silhouette at `age`; it holds its peak while the
//! unit is still coming out (negative ages).
inline float serviceSilhouette(float age)
{
	return ServiceShimmerPeak * (1 - smoothstep(clamp01(age / ServiceShimmerTicks)));
}

//! Opacity of a converted unit's silhouette, in its new team's colour, at `age`:
//! it brightens while the new colours gather onto it, then settles.
inline float convertSilhouette(float age)
{
	if (age < 0)
		return 0;
	if (age < GatherTicks)
		return ConvertShimmerPeak * easeIn(age / GatherTicks);
	return ConvertShimmerPeak * (1 - smoothstep(clamp01((age - GatherTicks) / ConvertShimmerTicks)));
}

//! Who watches a conversion: the team that gained the unit, the team that lost
//! it, or anyone else (allies, other players, replays).
enum class ConvertView : std::uint8_t { Gained, Lost, Neutral };

//! Hue, saturation and lightness (degrees, 0..1, 0..1) of each service's colour:
//! the silhouette and the tint of the building's white map glyph.
struct ServiceColor { float hue, saturation, lightness; };
inline ServiceColor serviceColor(Kind kind)
{
	switch (kind)
	{
	case Kind::Heal: return {125, 0.75f, 0.58f};
	case Kind::Feed: return {46, 1, 0.58f};
	default: return {45, 0.95f, 0.8f};
	}
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
	const float t = age / DustTicks, appear = clamp01(age / 4.5f);
	visit(Glow{0, -10 * t, lerp(18, 42, easeOut(t)), 0.5f * DustIntensity * appear * (1 - t), 0.82f});
	const float flash = clamp01(std::min(age / 36, age / (DustTicks * 0.6f)));
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
	const float appear = clamp01(age / 4.5f);
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
//! One of the building's map icons bursting out of a serviced unit.
struct Icon
{
	float x, y;  //!< offset from the exit tile's centre
	float size;  //!< drawn width and height
	float alpha;
	float stretch = 1; //!< height over width, for a heart squashing as it pops
};
//! The icons shoot up from the unit in a narrow fan (within 40 degrees of
//! straight up) with random distances, sizes, curls and start times, then drift
//! and fade. They share the space above the unit with a trained unit's level number.
template <class Visit>
void forEachServiceIcon(std::uint32_t seed, float age, Visit visit)
{
	Random random(seed ^ 0x1C0Au);
	constexpr float Pi = 3.14159265f;
	for (int k = 0; k < ServiceIcons; ++k)
	{
		// Draw every random number before any early exit so each icon keeps its identity.
		const float slot = (k + 0.5f) / ServiceIcons, jitter = (random.next() - 0.5f) * 1.6f / ServiceIcons;
		const float sweep = clamp01(lerp(slot, random.next(), 0.6f) + jitter);
		const float angle = (-90 + 50 * (sweep - 0.5f)) * Pi / 180;
		const float reach = IconReach * (0.45f + random.next() * 0.9f);
		const float delay = random.next() * IconDelayTicks, life = IconTicks * (0.65f + random.next() * 0.35f);
		const float speed = 1.1f + random.next() * 1.6f, curl = (random.next() - 0.5f) * 14;
		const float driftX = (random.next() - 0.5f) * 8, driftY = -4 - random.next() * 8;
		const float scale = 0.7f + random.next() * 0.6f, overshoot = 1.05f + random.next() * 0.3f;
		const float t = (age - delay) / life;
		if (t <= 0 || t >= 1)
			continue;
		const float out = easeOut(clamp01(t * speed));
		const float radius = lerp(3, reach, out);
		// The sideways curl peaks mid-flight; afterwards each icon drifts on its own.
		const float side = std::sin(out * Pi) * curl;
		const float x = std::cos(angle) * radius - std::sin(angle) * side + driftX * t;
		const float y = std::sin(angle) * radius + std::cos(angle) * side + driftY * t;
		const float pop = t < 0.12f ? lerp(0.5f, overshoot, t / 0.12f)
			: t < 0.25f ? lerp(overshoot, 1, (t - 0.12f) / 0.13f) : 1;
		const float alpha = t < 0.45f ? 1 : 1 - easeOut((t - 0.45f) / 0.55f);
		visit(Icon{x, y, IconSize * scale * pop, alpha});
	}
}

//! A trained unit's level number: map pixels above the unit's top and opacity at
//! `age`, as the combat level-up number moves (MapDrawer::drawUnit).
struct LevelNumber { float rise, alpha; };
inline LevelNumber levelNumber(float age)
{
	if (age < 0 || age >= LevelTicks)
		return {0, 0};
	return {2 * age, 1 - age / LevelTicks};
}

//! The new team's colours gathering onto a converted unit: the spawn's dust cloud
//! played backwards and faster, so the motes drift in from around and above it
//! and vanish on it at GatherTicks.
template <class Visit>
void forEachGatherMote(std::uint32_t seed, float age, Visit visit)
{
	if (age < 0 || age >= GatherTicks)
		return;
	forEachDustMote(seed, DustTicks * (1 - age / GatherTicks), visit);
}
//! A soft glow that swells on the unit as the motes arrive and fades after.
template <class Visit>
void forEachGatherGlow(float age, Visit visit)
{
	const float t = (age - GatherTicks * 0.6f) / 18;
	if (t <= 0 || t >= 1)
		return;
	const float alpha = t < 0.35f ? easeOut(t / 0.35f) : 1 - easeIn((t - 0.35f) / 0.65f);
	visit(Glow{0, -2, lerp(10, 22, easeOut(t)), 0.45f * DustIntensity * alpha, 0.9f});
}

//! The old team's colours falling off a converted unit: small motes start over
//! its body in turn and drop to the ground, spreading a little and fading.
template <class Visit>
void forEachShedMote(std::uint32_t seed, float age, Visit visit)
{
	if (age < 0 || age >= ShedTicks)
		return;
	Random random(seed ^ 0x5EDu);
	for (int k = 0; k < ShedMotes; ++k)
	{
		// Draw every random number before any early exit so each mote keeps its identity.
		const float x = (random.next() - 0.5f) * 18, y = -10 + random.next() * 16;
		const float delay = random.next() * (ShedTicks * 0.35f), life = 12 + random.next() * (ShedTicks * 0.65f - 12);
		const float fall = 10 + random.next() * 12, drift = (random.next() - 0.5f) * 10;
		const float size = 4.5f + random.next() * 4, phase = random.next() * 6.2831853f;
		const float t = (age - delay) / life;
		if (t <= 0 || t >= 1)
			continue;
		const float appear = clamp01(t / 0.15f), fading = appear * (1 - t * t);
		// A glint as each mote breaks off, then it dims as it falls.
		const float glint = std::pow(std::max(0.f, std::cos(t * 9 + phase)), 12.f) * (1 - t);
		visit(Sparkle{x + drift * t, y + fall * easeIn(t), size * (1 - 0.45f * t),
			0.9f * fading, 0.35f * glint});
	}
}

//! Which part of a converted unit's heart to draw.
enum class HeartPart : std::uint8_t { Whole, Left, Right };
//! The heart over a converted unit, offset from its centre. For the team that
//! gained it, a whole heart pops up, wobbling like jelly as it settles, and rises.
//! For the team that lost it, a heart appears above, trembles, cracks in two, and
//! its halves part and fall. Nothing for anyone else.
template <class Visit>
void forEachConvertHeart(ConvertView view, float age, Visit visit)
{
	if (view == ConvertView::Neutral)
		return;
	const float size = IconSize * 1.25f;
	if (view == ConvertView::Gained)
	{
		const float t = (age - GainedHeartStart) / HeartTicks;
		if (t <= 0 || t >= 1)
			return;
		const float pop = t < 0.12f ? lerp(0.4f, 1.2f, t / 0.12f) : t < 0.25f ? lerp(1.2f, 1, (t - 0.12f) / 0.13f) : 1;
		const float alpha = t < 0.55f ? 1 : 1 - easeOut((t - 0.55f) / 0.45f);
		// Flattened as it pops, then a few wobbles dying away.
		const float wobble = t < 0.12f ? -0.3f * (1 - t / 0.12f) : 0.2f * std::sin((t - 0.12f) * 34) * std::exp(-(t - 0.12f) * 7);
		visit(Icon{0, lerp(-18, -32, easeOut(t)), size * pop, alpha, 1 + wobble}, HeartPart::Whole);
		return;
	}
	const float t = (age - LostHeartStart) / HeartTicks;
	if (t <= 0 || t >= 1)
		return;
	constexpr float Crack = 0.35f;
	const float y = -28;
	if (t < Crack)
	{
		const float appear = clamp01(t / 0.12f);
		// A shiver just before it breaks.
		const float shake = t > 0.22f ? std::sin(t * 140) * 0.8f : 0;
		visit(Icon{shake, y, size * lerp(0.6f, 1, easeOut(appear)), appear}, HeartPart::Whole);
		return;
	}
	const float u = (t - Crack) / (1 - Crack);
	const float part = 1.5f + 4 * easeOut(u), drop = 22 * easeIn(u);
	const float alpha = 1 - easeIn(u);
	visit(Icon{-part, y + drop, size, alpha}, HeartPart::Left);
	visit(Icon{part, y + drop * 1.1f, size, alpha}, HeartPart::Right);
}
}
