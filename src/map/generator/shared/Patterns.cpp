// SPDX-License-Identifier: GPL-3.0-or-later
#include "Patterns.h"
#include "GenerationContext.h"
#include "LatticeNoise.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
namespace MapGeneration
{
namespace
{
// A box blur of radius r along one axis of the torus, by a running sum; the mean rounds towards minus
// infinity so it is exact integers everywhere.
void blurLine(const std::vector<int> &in, std::vector<int> &out, int r)
{
	const int n = int(in.size());
	out.assign(in.size(), 0);
	if (r <= 0)
	{
		out = in;
		return;
	}
	const int span = 2 * r + 1;
	std::int64_t sum = 0;
	for (int d = -r; d <= r; ++d)
		sum += in[((d % n) + n) % n];
	for (int i = 0; i < n; ++i)
	{
		const std::int64_t q = sum >= 0 ? sum / span : -((-sum + span - 1) / span);
		out[i] = int(q);
		sum -= in[((i - r) % n + n) % n];
		sum += in[(i + r + 1) % n];
	}
}

std::vector<int> boxBlur(const Torus &t, const std::vector<int> &field, int rx, int ry)
{
	std::vector<int> rows(field.size()), result(field.size()), line, out;
	line.resize(t.w);
	for (int y = 0; y < t.h; ++y)
	{
		for (int x = 0; x < t.w; ++x)
			line[x] = field[size_t(y) * t.w + x];
		blurLine(line, out, rx);
		for (int x = 0; x < t.w; ++x)
			rows[size_t(y) * t.w + x] = out[x];
	}
	line.resize(t.h);
	for (int x = 0; x < t.w; ++x)
	{
		for (int y = 0; y < t.h; ++y)
			line[y] = rows[size_t(y) * t.w + x];
		blurLine(line, out, ry);
		for (int y = 0; y < t.h; ++y)
			result[size_t(y) * t.w + x] = out[y];
	}
	return result;
}

// Two box passes, close to a Gaussian.
std::vector<int> smooth(const Torus &t, const std::vector<int> &field, int rx, int ry)
{
	return boxBlur(t, boxBlur(t, field, rx, ry), rx, ry);
}
} // namespace

std::vector<int> turingPattern(const Torus &t, const TuringStyle &style, std::mt19937 &rng)
{
	const int wavelength = std::max(4, style.wavelength);
	std::vector<int> field = periodicNoise(t.w, t.h, std::max(2, wavelength / 2), rng);
	for (int &v : field)
		v -= 32768;
	// Two box passes of radius r spread about as far as a Gaussian of deviation 0.8 r, and a difference
	// of two such blurs, one twice as wide, picks out waves a few radii long. Near radii of a tenth of
	// the wavelength and far radii of a fifth give bands within about 15% of the wavelength (measured
	// on 128-tile maps from 8 to 32 tiles).
	const auto radius = [&](int stretch, double share)
	{ return std::max(1, int(std::lround(wavelength * share * std::max(10, stretch) / 100.0))); };
	const int nearX = radius(style.stretchX, 1 / 10.5), nearY = radius(style.stretchY, 1 / 10.5);
	const int farX = std::max(nearX + 1, radius(style.stretchX, 2 / 10.5));
	const int farY = std::max(nearY + 1, radius(style.stretchY, 2 / 10.5));
	constexpr int kGain = 6;
	for (int pass = 0; pass < style.iterations; ++pass)
	{
		const std::vector<int> nearBlur = smooth(t, field, nearX, nearY);
		const std::vector<int> farBlur = smooth(t, field, farX, farY);
		std::int64_t total = 0;
		for (size_t i = 0; i < field.size(); ++i)
		{
			// Activation where the near blur beats the far one, inhibition where it doesn't: in
			// proportion, so crests grow smoothly rather than into flat plateaus.
			field[i] += int(std::int64_t(nearBlur[i] - farBlur[i]) * kGain / 4);
			total += field[i];
		}
		// Recentre on the mean and rescale to the peak, so the field neither drifts nor saturates.
		const int mean = int(total / std::int64_t(field.size()));
		int peak = 1;
		for (int &v : field)
		{
			v -= mean;
			peak = std::max(peak, std::abs(v));
		}
		for (int &v : field)
			v = int(std::int64_t(v) * 32768 / peak);
	}
	return field;
}

std::vector<int> stripePhase(const Torus &t, const StripeStyle &style, std::mt19937 &rng)
{
	const std::int64_t area = std::int64_t(t.w) * t.h;
	const std::vector<int> warp =
		style.warpPercent ? fractalNoise(t.w, t.h, std::max(2, style.warpPeriod), 3, rng)
						  : std::vector<int>(size_t(area), 32768);
	std::vector<int> phase(static_cast<size_t>(area));
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const size_t i = size_t(y) * t.w + x;
			// (acrossX * x / w + acrossY * y / h) whole turns, in 65536ths, exactly.
			std::int64_t turns =
				(std::int64_t(style.acrossX) * x * t.h + std::int64_t(style.acrossY) * y * t.w) *
				65536 / area;
			turns += std::int64_t(warp[i] - 32768) * style.warpPercent / 100;
			phase[i] = int(((turns % 65536) + 65536) % 65536);
		}
	return phase;
}

StripeStyle alongStripes(const Torus &t, const StripeStyle &across)
{
	StripeStyle along;
	along.warpPercent = 0;
	if (t.w >= t.h)
	{
		along.acrossX = across.acrossY * (t.w / t.h);
		along.acrossY = -across.acrossX;
	}
	else
	{
		along.acrossX = across.acrossY;
		along.acrossY = -across.acrossX * (t.h / t.w);
	}
	return along;
}

double stripeSpacing(const Torus &t, const StripeStyle &style)
{
	const double kx = double(style.acrossX) / t.w, ky = double(style.acrossY) / t.h;
	const double k = std::hypot(kx, ky);
	return k > 0 ? 1.0 / k : double(std::max(t.w, t.h));
}

double stripeNormal(const Torus &t, const StripeStyle &style)
{
	return std::atan2(double(style.acrossY) / t.h, double(style.acrossX) / t.w);
}

double stripeHeading(const Torus &t, const StripeStyle &style)
{
	return stripeNormal(t, style) + kPi / 2;
}

std::vector<int> upwindSteps(const Torus &t, const std::vector<unsigned char> &mask, int dx, int dy,
							 int reach)
{
	const int n = t.w * t.h, length = std::max(std::abs(dx), std::abs(dy));
	std::vector<int> steps(size_t(n), -1);
	if (!length)
	{
		for (int i = 0; i < n; ++i)
			steps[i] = mask[i] ? 0 : -1;
		return steps;
	}
	// The upwind offset k Chebyshev steps back, rounded half away from zero, in integers.
	const auto back = [&](int k, int d)
	{
		const int num = 2 * k * d;
		return -(num >= 0 ? (num + length) / (2 * length) : -((-num + length) / (2 * length)));
	};
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = y * t.w + x;
			if (mask[i])
			{
				steps[i] = 0;
				continue;
			}
			for (int k = 1; k <= reach; ++k)
				if (mask[t.at(x + back(k, dx), y + back(k, dy))])
				{
					steps[i] = k;
					break;
				}
		}
	return steps;
}
std::vector<unsigned char> runsAndGaps(int length, int runLow, int runHigh, int gapLow, int gapHigh,
									   GenerationContext &context, const char *stream)
{
	std::vector<unsigned char> on(size_t(std::max(0, length)), 0);
	for (int u = 0; u < length;)
	{
		const int run = runLow + int(context.bounded(stream, std::uint32_t(runHigh - runLow + 1)));
		const int gap = gapLow + int(context.bounded(stream, std::uint32_t(gapHigh - gapLow + 1)));
		for (int k = 0; k < run && u < length; ++k)
			on[u++] = 1;
		u += gap;
	}
	return on;
}
} // namespace MapGeneration
