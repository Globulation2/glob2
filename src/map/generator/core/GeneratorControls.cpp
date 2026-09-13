// SPDX-License-Identifier: GPL-3.0-or-later
#include "GeneratorControls.h"
#include "GenerationRequest.h"
#include "GeneratorRegistry.h"
#include "Team.h"
#include <algorithm>
#include <stdexcept>
GeneratorControl GeneratorControl::toggle(std::string id, const char *label, bool on,
										  ControlGroup group)
{
	GeneratorControl c{std::move(id), label, 0, 1, 1, on ? 1 : 0, group};
	c.kind = GeneratorControl::Kind::Toggle;
	return c;
}
GeneratorControl GeneratorControl::percentage(std::string id, const char *label, int maximum)
{
	return {std::move(id), label, 0, maximum, 25, 100, ControlGroup::Resources};
}
int GeneratorControl::normalize(int v) const
{
	if (!allowedValues.empty())
	{
		auto hi = std::lower_bound(allowedValues.begin(), allowedValues.end(), v);
		if (hi == allowedValues.begin())
			return *hi;
		if (hi == allowedValues.end())
			return allowedValues.back();
		return std::int64_t(v) - *(hi - 1) < std::int64_t(*hi) - v ? *(hi - 1) : *hi;
	}
	v = std::clamp(v, minimum, maximum);
	return int(std::min(std::int64_t(maximum),
						minimum + ((std::int64_t(v) - minimum + step / 2) / step) * step));
}
std::vector<int> GeneratorControl::values() const
{
	if (!allowedValues.empty())
		return allowedValues;
	std::vector<int> result;
	for (std::int64_t v = minimum; v <= maximum; v += step)
		result.push_back(int(v));
	return result;
}
int GeneratorControl::indexOf(int value) const
{
	const auto domain = values();
	return int(std::lower_bound(domain.begin(), domain.end(), normalize(value)) - domain.begin());
}
int GeneratorControl::valueAt(int index) const
{
	return values().at(index);
}
int GeneratorControl::displayValue(int value) const
{
	return powerOfTwo ? (1 << value) : value;
}
int GeneratorControl::get(const GenerationRequest &r) const
{
	if (id == "width")
		return r.wDec;
	if (id == "height")
		return r.hDec;
	if (id == "teams")
		return r.nbTeams;
	if (id == "workers")
		return r.nbWorkers;
	return r.option(id);
}
void GeneratorControl::set(GenerationRequest &r, int value) const
{
	int v = normalize(value);
	if (id == "width")
		r.wDec = v;
	else if (id == "height")
		r.hDec = v;
	else if (id == "teams")
		r.nbTeams = v;
	else if (id == "workers")
		r.nbWorkers = v;
	else
		r.options[id] = v;
}
const std::vector<GeneratorControl> &sharedGeneratorControls()
{
	static const std::vector<GeneratorControl> controls = {
		{"width", "Width", 6, 9, 1, 8, ControlGroup::Shared, true},
		{"height", "Height", 6, 9, 1, 8, ControlGroup::Shared, true},
		{"teams", "Colonies", 1, Team::MAX_COUNT, 1, 4, ControlGroup::Shared},
		{"workers", "Starting workers", 1, 8, 1, 4, ControlGroup::Shared}};
	return controls;
}
GenerationRequest::GenerationRequest()
{
	resourceAmounts.fill(7);
	for (const auto &c : sharedControls())
		c.set(*this, c.defaultValue);
	setMethodDefaults(eUNIFORM);
}
void GenerationRequest::setMethodDefaults(int id)
{
	setMethodDefaults(id, GeneratorRegistry::builtins());
}
void GenerationRequest::setMethodDefaults(int id, const GeneratorRegistry &registry)
{
	method = id;
	options.clear();
	for (const auto &c : registry.at(id).controls)
		c.set(*this, c.defaultValue);
}
const std::vector<GeneratorControl> &GenerationRequest::controls(int id)
{
	return GeneratorRegistry::builtins().at(id).controls;
}
const char *GenerationRequest::methodName(int id)
{
	return GeneratorRegistry::builtins().at(id).nameKey;
}
const GeneratorControl &GenerationRequest::control(int method, const std::string &id)
{
	for (const auto &c : controls(method))
		if (c.id == id)
			return c;
	for (const auto &c : sharedControls())
		if (c.id == id)
			return c;
	throw std::invalid_argument("Unknown generator control: " + id);
}
bool GenerationRequest::hasTerrainWeight() const
{
	return hasTerrainWeight(controls(method));
}
bool GenerationRequest::hasTerrainWeight(const std::vector<Control> &definitions) const
{
	int total = 0;
	bool weighted = false;
	for (const auto &c : definitions)
		if (c.terrainWeight)
		{
			weighted = true;
			total += c.get(*this);
		}
	return !weighted || total > 0;
}
void GenerationHistory::select(GenerationRequest &current, int id)
{
	select(current, id, GeneratorRegistry::builtins());
}
void GenerationHistory::select(GenerationRequest &current, int id,
							   const GeneratorRegistry &registry)
{
	if (id == current.method)
		return;
	settings[current.method] = current;
	auto it = settings.find(id);
	GenerationRequest next;
	if (it != settings.end())
		next = it->second;
	else
		next.setMethodDefaults(id, registry);
	for (const auto &c : sharedGeneratorControls())
		c.set(next, c.get(current));
	next.terrainType = current.terrainType;
	next.seed = current.seed;
	current = next;
}
