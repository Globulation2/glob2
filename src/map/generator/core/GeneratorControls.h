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
