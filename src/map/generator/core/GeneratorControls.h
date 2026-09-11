// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>
struct GenerationRequest;
class MapGenerationDescriptor;
enum class ControlGroup
{
	Terrain,
	Resources,
	Layout,
	Shared
};
enum class ControlKind
{
	// A number from minimum to maximum in steps, or one of allowedValues.
	Range,
	// An on/off switch, stored as 0 or 1 and shown as a checkbox.
	Toggle
};
struct GeneratorControl
{
	std::string id;
	const char *label;
	int minimum, maximum, step, defaultValue;
	ControlGroup group = ControlGroup::Terrain;
	bool powerOfTwo = false;
	bool terrainWeight = false;
	// Optional ordered domain, e.g. {4, 8, 16}; values are stored literally.
	std::vector<int> allowedValues;
	ControlKind kind = ControlKind::Range;
	static GeneratorControl toggle(std::string id, const char *label, bool on,
								   ControlGroup group = ControlGroup::Layout);
	// A resource amount as a percentage of the generator's own default, which is 100.
	static GeneratorControl percentage(std::string id, const char *label, int maximum = 300);
	bool isToggle() const { return kind == ControlKind::Toggle; }
	std::vector<int> values() const;
	int indexOf(int value) const;
	int valueAt(int index) const;
	int displayValue(int value) const;
	int normalize(int value) const;
	int get(const GenerationRequest &) const;
	void set(GenerationRequest &, int) const;
	int get(const MapGenerationDescriptor &) const;
	void set(MapGenerationDescriptor &, int) const;
};
const std::vector<GeneratorControl> &sharedGeneratorControls();
