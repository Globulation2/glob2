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

Emphases::Emphases(GenerationContext &context, const char *stream,
				   std::vector<const char *> optional, int least, int most)
{
	const int fewest = std::clamp(least, 0, int(optional.size()));
	const int many = std::clamp(most, fewest, int(optional.size()));
	const int wanted =
		fewest + int(context.bounded(stream, std::uint32_t(std::max(1, many - fewest + 1))));
	context.shuffle(optional.begin(), optional.end(), stream);
	for (int i = 0; i < wanted && i < int(optional.size()); ++i)
		chosen.push_back(optional[i]);
}

bool Emphases::on(const char *name) const
{
	for (const char *have : chosen)
		if (std::string(have) == name)
			return true;
	return false;
}

void Emphases::report(GenerationTelemetry &telemetry, const std::string &key) const
{
	telemetry.measure(key + ".count", int(chosen.size()));
	for (const char *name : chosen)
		telemetry.choice(key, name);
}

double Brief::target(const char *name, double least, double most)
{
	const double drawn = drawnTarget(context, (map + "-brief").c_str(), least, most);
	context.telemetry.measure(map + ".brief." + name, drawn);
	return drawn;
}

void Brief::choose(std::vector<const char *> optional, int least, int most)
{
	emphases = Emphases(context, (map + "-brief").c_str(), std::move(optional), least, most);
	emphases.report(context.telemetry, map + ".brief.emphases");
}

double drawnTarget(GenerationContext &context, const char *stream, double least, double most)
{
	constexpr std::uint32_t kSteps = 1000;
	return least + (most - least) * double(context.bounded(stream, kSteps)) / double(kSteps - 1);
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
