// SPDX-License-Identifier: GPL-3.0-or-later
#include <ScrollPhysics.h>
#include <algorithm>
#include <cmath>

namespace GAGCore
{
namespace
{
constexpr double PI = 3.14159265358979323846;
}

ScrollPhysicsConfig ScrollPhysicsConfig::scaled(double unit) const
{
	ScrollPhysicsConfig out = *this;
	out.minFlingVelocity *= unit;
	out.maxFlingVelocity *= unit;
	out.maxBounceVelocity *= unit;
	out.stopVelocity *= unit;
	out.settleDistance *= unit;
	return out;
}

ScrollTuning &scrollTuning()
{
	static ScrollTuning tuning;
	return tuning;
}

double decelerationRateForLevel(int level)
{
	const int v = std::clamp(level, 1, 100);
	return 1.0 - 0.002 * std::pow(2.0, (50 - v) / 25.0);
}

double rubberBandForLevel(int level)
{
	return 0.55 * std::clamp(level, 1, 100) / 50.0;
}

namespace ScrollPresets
{
namespace
{
ScrollPhysicsConfig tuned(int momentumLevel, int bounceLevel)
{
	ScrollPhysicsConfig config;
	const auto &tuning = scrollTuning();
	config.momentum = momentumLevel > 0;
	if (config.momentum)
		config.decelerationRate = decelerationRateForLevel(momentumLevel);
	config.bounce = bounceLevel > 0 && !tuning.reducedMotion;
	if (config.bounce)
		config.rubberBandCoefficient = rubberBandForLevel(bounceLevel);
	return config;
}
} // namespace

ScrollPhysicsConfig widget() { return tuned(scrollTuning().momentum, scrollTuning().bounce); }
ScrollPhysicsConfig hudPanel() { return widget(); }
ScrollPhysicsConfig editorTray() { return widget(); }
ScrollPhysicsConfig mouse()
{
	ScrollPhysicsConfig config;
	config.momentum = false;
	config.bounce = false;
	return config;
}
ScrollPhysicsConfig mapViewport()
{
	ScrollPhysicsConfig config = tuned(scrollTuning().mapMomentum, 0);
	config.wrap = true;
	return config;
}
} // namespace ScrollPresets

Ticks widenTicks(std::uint32_t stamp, Ticks reference)
{
	constexpr Ticks period = Ticks(1) << 32, half = Ticks(1) << 31;
	Ticks candidate = (reference & ~(period - 1)) | stamp;
	if (candidate > reference && candidate - reference > half && candidate >= period)
		candidate -= period;
	else if (reference > candidate && reference - candidate > half)
		candidate += period;
	return candidate;
}

void VelocityTracker::reset()
{
	head = 0;
	count = 0;
}

void VelocityTracker::add(Ticks t, double position)
{
	ring[head] = {t, position};
	head = (head + 1) % ring.size();
	if (count < ring.size())
		++count;
}

double VelocityTracker::velocity(Ticks t) const
{
	if (count == 0)
		return 0;
	// Newest first.
	std::size_t n = 0;
	double tau[24], x[24];
	for (std::size_t i = 0; i < count; ++i)
	{
		const Sample &s = ring[(head + ring.size() - 1 - i) % ring.size()];
		if (s.t > t)
			continue;
		const Ticks age = t - s.t;
		if (i == 0 && age > stale)
			return 0;
		if (age > window)
			break;
		tau[n] = -double(age);
		x[n] = s.x;
		++n;
	}
	if (n < 2 || tau[0] == tau[n - 1])
		return 0;
	if (n == 2)
		return (x[0] - x[1]) / (tau[0] - tau[1]);
	// Least squares x = a + b*tau + c*tau^2; b is the velocity at tau = 0.
	double s0 = n, s1 = 0, s2 = 0, s3 = 0, s4 = 0, sx = 0, sxt = 0, sxt2 = 0;
	for (std::size_t i = 0; i < n; ++i)
	{
		const double t1 = tau[i], t2 = t1 * t1;
		s1 += t1;
		s2 += t2;
		s3 += t2 * t1;
		s4 += t2 * t2;
		sx += x[i];
		sxt += x[i] * t1;
		sxt2 += x[i] * t2;
	}
	// Solve the 3x3 normal equations by Cramer's rule.
	auto det3 = [](double a, double b, double c, double d, double e, double f, double g, double h, double i)
	{ return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g); };
	const double det = det3(s0, s1, s2, s1, s2, s3, s2, s3, s4);
	if (std::fabs(det) < 1e-9)
		return (x[0] - x[n - 1]) / (tau[0] - tau[n - 1]);
	const double detB = det3(s0, sx, s2, s1, sxt, s3, s2, sxt2, s4);
	return detB / det;
}

ScrollAxis::ScrollAxis(const ScrollPhysicsConfig &config) : cfg(config) {}

void ScrollAxis::setConfig(const ScrollPhysicsConfig &config) { cfg = config; }

double ScrollAxis::clampedOffset() const
{
	if (cfg.wrap)
		return value;
	return std::clamp(value, low, high);
}

double ScrollAxis::rubber(double excess) const
{
	if (!cfg.bounce || excess <= 0)
		return 0;
	const double d = std::max(extent, 1.0), c = cfg.rubberBandCoefficient;
	return excess * d * c / (d + c * excess);
}

double ScrollAxis::unrubber(double stretched) const
{
	if (!cfg.bounce || stretched <= 0)
		return 0;
	const double d = std::max(extent, 1.0), c = cfg.rubberBandCoefficient;
	const double s = std::min(stretched, d - 1e-6);
	return d * s / (c * (d - s));
}

double ScrollAxis::lambda() const { return 2 * PI / std::max(cfg.springResponse, 1.0); }

void ScrollAxis::setBounds(double minimum, double maximum, double viewport)
{
	low = minimum;
	high = std::max(maximum, minimum);
	extent = std::max(viewport, 1.0);
	if (cfg.wrap)
		return;
	if (state == Phase::Idle && !held)
		value = std::clamp(value, low, high);
	else if (state == Phase::Dragging)
		drag(t0, 0);
}

void ScrollAxis::setOffset(double offset)
{
	state = Phase::Idle;
	held = false;
	value = offset;
	if (!cfg.wrap)
		value = std::clamp(value, low, high);
}

void ScrollAxis::beginDrag(Ticks t)
{
	state = Phase::Dragging;
	held = false;
	tracker.reset();
	t0 = t;
	if (cfg.wrap)
	{
		value = 0;
		anchor = 0;
		raw = 0;
	}
	else if (value > high)
	{
		anchor = high;
		raw = unrubber(value - high);
	}
	else if (value < low)
	{
		anchor = low;
		raw = -unrubber(low - value);
	}
	else
	{
		anchor = value;
		raw = 0;
	}
	tracker.add(t, raw);
}

void ScrollAxis::drag(Ticks t, double delta)
{
	if (state != Phase::Dragging)
		beginDrag(t);
	raw += delta;
	t0 = t;
	const double target = anchor + raw;
	if (cfg.wrap)
		value = target;
	else if (target > high)
		value = high + rubber(target - high);
	else if (target < low)
		value = low - rubber(low - target);
	else
		value = target;
	if (delta != 0 || tracker.size() == 0)
		tracker.add(t, raw);
}

void ScrollAxis::startBounce(Ticks t, double at, double overshoot, double velocity)
{
	state = Phase::Bouncing;
	t0 = t;
	edge = at;
	c1 = overshoot;
	c2 = std::clamp(velocity, -cfg.maxBounceVelocity, cfg.maxBounceVelocity) + lambda() * overshoot;
}

void ScrollAxis::endDrag(Ticks t)
{
	if (state != Phase::Dragging)
		return;
	const double v = std::clamp(tracker.velocity(t), -cfg.maxFlingVelocity, cfg.maxFlingVelocity);
	if (!cfg.wrap && (value > high || value < low))
	{
		if (cfg.bounce)
		{
			const double at = value > high ? high : low;
			startBounce(t, at, value - at, v);
		}
		else
			setOffset(value);
		return;
	}
	if (!cfg.momentum || std::fabs(v) < cfg.minFlingVelocity)
	{
		state = Phase::Idle;
		return;
	}
	state = Phase::Decelerating;
	t0 = t;
	x0 = value;
	v0 = v;
}

void ScrollAxis::settle(Ticks t)
{
	if (state != Phase::Idle || cfg.wrap)
		return;
	held = false;
	if (value > high || value < low)
	{
		if (cfg.bounce)
		{
			const double at = value > high ? high : low;
			startBounce(t, at, value - at, 0);
		}
		else
			value = std::clamp(value, low, high);
	}
}

void ScrollAxis::interrupt()
{
	if (!isAnimating())
		return;
	held = state == Phase::Bouncing;
	state = Phase::Idle;
}

double ScrollAxis::projectedEnd() const
{
	if (state != Phase::Decelerating)
		return value;
	const double end = x0 - v0 / std::log(cfg.decelerationRate);
	return cfg.wrap ? end : std::clamp(end, low, high);
}

double ScrollAxis::step(Ticks t)
{
	if (state == Phase::Decelerating)
	{
		const double L = std::log(cfg.decelerationRate);
		const double dt = t > t0 ? double(t - t0) : 0.0;
		const double decay = std::pow(cfg.decelerationRate, dt);
		double x = x0 + v0 * (decay - 1) / L;
		const double v = v0 * decay;
		if (!cfg.wrap && ((v0 > 0 && x > high) || (v0 < 0 && x < low)))
		{
			const double at = v0 > 0 ? high : low;
			// Time at which the coast reaches the edge.
			const double inner = 1 + (at - x0) * L / v0;
			const double te = inner > 0 ? std::max(0.0, std::log(inner) / L) : 0.0;
			const double ve = v0 * std::pow(cfg.decelerationRate, te);
			if (cfg.bounce)
			{
				startBounce(t0 + Ticks(std::llround(te)), at, 0, ve);
				return step(t);
			}
			state = Phase::Idle;
			value = at;
			return value;
		}
		if (std::fabs(v) < cfg.stopVelocity)
		{
			value = projectedEnd();
			state = Phase::Idle;
			return value;
		}
		value = x;
		return value;
	}
	if (state == Phase::Bouncing)
	{
		const double tau = t > t0 ? double(t - t0) : 0.0;
		const double k = lambda(), e = std::exp(-k * tau);
		const double x = (c1 + c2 * tau) * e;
		const double v = (c2 - k * (c1 + c2 * tau)) * e;
		if (std::fabs(x) < cfg.settleDistance && std::fabs(v) < cfg.stopVelocity)
		{
			value = edge;
			state = Phase::Idle;
			return value;
		}
		value = edge + x;
		return value;
	}
	return value;
}

void ScrollMotion::setConfig(const ScrollPhysicsConfig &config)
{
	x.setConfig(config);
	y.setConfig(config);
}

void ScrollMotion::beginDrag(Ticks t)
{
	x.beginDrag(t);
	y.beginDrag(t);
}

void ScrollMotion::drag(Ticks t, double dx, double dy)
{
	x.drag(t, dx);
	y.drag(t, dy);
}

void ScrollMotion::endDrag(Ticks t)
{
	x.endDrag(t);
	y.endDrag(t);
}

void ScrollMotion::interrupt()
{
	x.interrupt();
	y.interrupt();
}

std::pair<double, double> ScrollMotion::stepDelta(Ticks t)
{
	const double px = x.offset(), py = y.offset();
	return {x.step(t) - px, y.step(t) - py};
}
} // namespace GAGCore
