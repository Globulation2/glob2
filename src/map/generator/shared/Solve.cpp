// SPDX-License-Identifier: GPL-3.0-or-later
#include "Solve.h"
namespace MapGeneration
{
bool accept(double rise, double heat, GenerationContext &context, const char *stream)
{
	if (rise <= 0)
		return true;
	if (heat <= 0)
		return false;
	// The draw is an integer one so the decision is exactly reproducible from the seed: a bounded
	// draw over a power of two, compared against the Metropolis probability scaled to it.
	constexpr std::uint32_t kDraws = 1 << 20;
	return double(context.bounded(stream, kDraws)) < std::exp(-rise / heat) * double(kDraws);
}

double imbalance(const std::vector<double> &shares)
{
	if (shares.size() < 2)
		return 0;
	double least = shares[0], most = shares[0], total = 0;
	for (const double share : shares)
	{
		least = std::min(least, share);
		most = std::max(most, share);
		total += share;
	}
	const double mean = total / double(shares.size());
	if (!(mean > 0))
		return 1.0;
	return (most - least) / mean + (least > 0 ? 0.0 : 1.0);
}

int spread(const std::vector<int> &shares)
{
	if (shares.empty())
		return 0;
	const auto [least, most] = std::minmax_element(shares.begin(), shares.end());
	return *most - *least;
}

void reportObjective(GenerationTelemetry &telemetry, const std::string &key,
					 const Objective &objective)
{
	// The keys are built from the terms' names, so this is guarded: nothing is spent composing
	// strings on the ordinary generation path where nobody is collecting.
	if (!telemetry.enabled())
		return;
	for (int i = 0; i < objective.count; ++i)
	{
		const auto &term = objective.terms[i];
		telemetry.measure(key + "." + term.name + ".residual", term.residual);
		telemetry.measure(key + "." + term.name + ".weighted", term.weighted());
	}
	telemetry.measure(key + ".total", objective.total());
}

void reportSolve(GenerationTelemetry &telemetry, const std::string &key, const SolveReport &report)
{
	telemetry.measure(key + ".proposed", report.proposed);
	telemetry.measure(key + ".taken", report.taken);
	telemetry.measure(key + ".kept", report.kept);
	telemetry.measure(key + ".cost-before", report.before);
	telemetry.measure(key + ".cost-after", report.after);
}
} // namespace MapGeneration
