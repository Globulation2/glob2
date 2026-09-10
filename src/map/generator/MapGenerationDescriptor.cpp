// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <assert.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <Stream.h>

#include "MapGenerationDescriptor.h"
#include "Marshaling.h"
#include "Utilities.h"

namespace
{
using D = MapGenerationDescriptor;
using C = D::Control;
using G = D::ControlGroup;
// Physical descriptor fields have one base definition. Per-method overrides below
// supply the measured recipe, and are also the controls displayed by both UIs.
const std::vector<C> baseControls = {
	{"Water weight", &D::waterRatio, 0, 100, 1, 50},
	{"Sand weight", &D::sandRatio, 0, 100, 1, 0},
	{"Grass weight", &D::grassRatio, 0, 100, 1, 50},
	{"Desert weight", &D::desertRatio, 0, 100, 1, 0},
	{"Smoothing", &D::smooth, 1, 8, 1, 4},
	{"River width", &D::riverDiameter, 20, 65, 5, 50},
	{"Extra islands", &D::extraIslands, 0, 8, 1, 0},
	{"Lake density", &D::craterDensity, 10, 50, 5, 50},
	{"Island size", &D::oldIslandSize, 50, 70, 1, 65},
	{"Beach size", &D::oldBeach, 0, 4, 1, 1},
	{"Fruit", &D::fruitRatio, 0, 64, 1, 4, G::Resources},
	{"Repeat landscape", nullptr, 0, 5, 1, 0, G::Layout, true},
};
const std::array<std::vector<C>, D::METHOD_COUNT> methodControls = []
{
	std::array<std::vector<C>, D::METHOD_COUNT> result;
	for (int m = 1; m < D::METHOD_COUNT; ++m)
	{
		auto &controls = result[m];
		for (const auto &c : baseControls)
		{
			bool include = false;
			if (m <= D::eCRATERLAKES || m == D::eOLDRANDOM)
			{
				include =
					c.field == &D::waterRatio || c.field == &D::grassRatio || c.field == &D::smooth;
				include |= m != D::eSWAMP && c.field == &D::sandRatio;
				include |= m >= D::eRIVER && m <= D::eCRATERLAKES && c.field == &D::desertRatio;
				include |=
					m <= D::eCRATERLAKES && (c.group == G::Resources || c.group == G::Layout);
			}
			include |= m == D::eRIVER && c.field == &D::riverDiameter;
			include |= m == D::eISLANDS && c.field == &D::extraIslands;
			include |= m == D::eCRATERLAKES && c.field == &D::craterDensity;
			include |=
				m == D::eOLDISLANDS && (c.field == &D::oldIslandSize || c.field == &D::oldBeach);
			if (include)
				controls.push_back(c);
		}
	}
	auto preset = [&](D::Method m, Sint32 D::*field, int value)
	{
		for (auto &c : result[m])
			if (c.field == field)
				c.defaultValue = value;
	};
	preset(D::eSWAMP, &D::waterRatio, 35);
	preset(D::eSWAMP, &D::grassRatio, 60);
	preset(D::eSWAMP, &D::smooth, 6);
	preset(D::eRIVER, &D::waterRatio, 45);
	preset(D::eRIVER, &D::sandRatio, 3);
	preset(D::eRIVER, &D::grassRatio, 75);
	preset(D::eRIVER, &D::riverDiameter, 35);
	preset(D::eISLANDS, &D::waterRatio, 55);
	preset(D::eISLANDS, &D::sandRatio, 3);
	preset(D::eISLANDS, &D::grassRatio, 75);
	preset(D::eCRATERLAKES, &D::waterRatio, 25);
	preset(D::eCRATERLAKES, &D::sandRatio, 3);
	preset(D::eCRATERLAKES, &D::grassRatio, 75);
	preset(D::eCRATERLAKES, &D::smooth, 6);
	preset(D::eCRATERLAKES, &D::craterDensity, 25);
	result[D::eCRATERLAKES].push_back({"Lake size", &D::riverDiameter, 10, 40, 5, 25});
	result[D::eCONCRETEISLANDS] = {{"Channel width", &D::riverDiameter, 5, 8, 1, 5},
								   {"Extra islands", &D::extraIslands, 0, 6, 1, 3}};
	result[D::eISLES] = {{"Island size", &D::grassRatio, 45, 65, 5, 60},
						 {"Land bridge width", &D::riverDiameter, 3, 6, 1, 4}};
	preset(D::eOLDRANDOM, &D::waterRatio, 40);
	preset(D::eOLDRANDOM, &D::sandRatio, 4);
	preset(D::eOLDRANDOM, &D::grassRatio, 60);
	preset(D::eOLDRANDOM, &D::smooth, 3);
	for (auto &controls : result)
		std::stable_sort(controls.begin(), controls.end(),
						 [](const C &a, const C &b) { return a.group < b.group; });
	return result;
}();
} // namespace

int MapGenerationDescriptor::Control::get(const MapGenerationDescriptor &d) const
{
	return field ? d.*field : static_cast<int>(d.logRepeatAreaTimes);
}
int MapGenerationDescriptor::Control::normalize(int value) const
{
	value = std::clamp(value, minimum, maximum);
	return std::min(maximum, minimum + ((value - minimum + step / 2) / step) * step);
}
void MapGenerationDescriptor::Control::set(MapGenerationDescriptor &d, int value) const
{
	if (field)
		d.*field = normalize(value);
	else
		d.logRepeatAreaTimes = normalize(value);
}
const std::vector<MapGenerationDescriptor::Control> &
MapGenerationDescriptor::controls(Method method)
{
	assert(method >= eUNIFORM && method <= eOLDISLANDS);
	return methodControls[method];
}
const std::vector<MapGenerationDescriptor::Control> &MapGenerationDescriptor::sharedControls()
{
	static const std::vector<Control> shared = {
		{"Width", &D::wDec, 6, 9, 1, 7, G::Shared, true},
		{"Height", &D::hDec, 6, 9, 1, 7, G::Shared, true},
		{"Colonies", &D::nbTeams, 1, Team::MAX_COUNT, 1, 4, G::Shared},
		{"Starting workers", &D::nbWorkers, 1, 8, 1, 4, G::Shared}};
	return shared;
}
const MapGenerationDescriptor::Control &MapGenerationDescriptor::control(Method method,
																		 const char *label)
{
	for (const auto &c : controls(method))
		if (std::strcmp(c.label, label) == 0)
			return c;
	for (const auto &c : sharedControls())
		if (std::strcmp(c.label, label) == 0)
			return c;
	throw std::logic_error("Unknown map generator control");
}
const char *MapGenerationDescriptor::methodName(Method method)
{
	static const char *names[] = {"uniform terrain", "Swamp",		 "River",
								  "Islands",		 "Crater lakes", "Concrete islands",
								  "Isles",			 "Old random",	 "Old islands"};
	assert(method >= eUNIFORM && method <= eOLDISLANDS);
	return names[method]; // Stable translation keys; English values carry the new names.
}
MapGenerationDescriptor::MapGenerationDescriptor()
{
	terrainType=GRASS;
	for (const auto &c : sharedControls())
		c.set(*this, c.defaultValue);
	setMethodDefaults(eUNIFORM);
	for (int i = 0; i < MAX_NB_RESOURCES; ++i)
		resource[i] = 7;
}
void MapGenerationDescriptor::setMethodDefaults(Method newMethod)
{
	method = newMethod;
	for (const auto &c : baseControls)
		c.set(*this, c.defaultValue);
	// These legacy serialized fields are unused by resource placement. Do not
	// advertise inert sliders; retain their historic values for descriptor compatibility.
	wheatRatio = woodRatio = stoneRatio = algaeRatio = 50;
	for (const auto &c : controls(method))
		c.set(*this, c.defaultValue);
}
bool MapGenerationDescriptor::hasTerrainWeight() const
{
	if (method == eSWAMP)
		return waterRatio + grassRatio > 0;
	if (method == eOLDRANDOM)
		return waterRatio + sandRatio + grassRatio > 0;
	if (method >= eRIVER && method <= eCRATERLAKES)
		return waterRatio + sandRatio + grassRatio + desertRatio > 0;
	return true;
}
MapGenerationHistory::MapGenerationHistory()
{
	for (int m = 0; m <= MapGenerationDescriptor::eOLDISLANDS; ++m)
		settings[m].setMethodDefaults(static_cast<MapGenerationDescriptor::Method>(m));
}
void MapGenerationHistory::select(MapGenerationDescriptor &current,
								  MapGenerationDescriptor::Method method)
{
	if (method == current.method)
		return;
	assert(method >= 0 && method <= MapGenerationDescriptor::eOLDISLANDS);
	settings[current.method] = current;
	auto next = settings[method];
	for (const auto &c : MapGenerationDescriptor::sharedControls())
		c.set(next, c.get(current));
	next.terrainType = current.terrainType;
	current = next;
}

MapGenerationDescriptor::~MapGenerationDescriptor()
{
	// Tuut-tuut bom-bom
}

Uint8 *MapGenerationDescriptor::getData()
{
	assert(DATA_SIZE==100+MAX_NB_RESOURCES*4);
	
	addSint32(data, wDec, 0);
	addSint32(data, hDec, 4);

	addSint32(data, (Sint32)terrainType, 8);

	addSint32(data, (Sint32)method, 12);
	addSint32(data, waterRatio, 16);
	addSint32(data, sandRatio, 20);
	addSint32(data, grassRatio, 24);
	addSint32(data, desertRatio, 28);
	
	addSint32(data, wheatRatio, 32);
	addSint32(data, woodRatio, 36);
	addSint32(data, algaeRatio, 40);
	addSint32(data, stoneRatio, 44);	
	addSint32(data, riverDiameter, 48);
	
	addSint32(data, craterDensity, 52);
	addSint32(data, extraIslands, 56);
	addSint32(data, smooth, 60);

	addUint32(data, nbWorkers, 64);
	addUint32(data, nbTeams, 68);
	
	addUint32(data, oldIslandSize, 72);
	addUint32(data, oldBeach, 76);
	addUint32(data, fruitRatio, 80);

	addUint32(data, logRepeatAreaTimes, 84);

	for (unsigned i=0; i<MAX_NB_RESOURCES; i++)
		addSint32(data, resource[i], 88+i*4);

	return data;
}

bool MapGenerationDescriptor::setData(const Uint8 *data, int dataLength)
{
	assert(DATA_SIZE==100+MAX_NB_RESOURCES*4);
	assert(getDataLength()==DATA_SIZE);
	assert(getDataLength()==dataLength);
	
	wDec=getSint32(data, 0);
	hDec=getSint32(data, 4);
	
	terrainType=(TerrainType)getSint32(data, 8);

	method=(Method)getSint32(data, 12);
	waterRatio=getSint32(data, 16);
	sandRatio=getSint32(data, 20);
	grassRatio=getSint32(data, 24);
	desertRatio = getSint32(data, 28);
	
	wheatRatio = getSint32(data, 32);
	woodRatio = getSint32(data, 36);
	algaeRatio = getSint32(data, 40);
	stoneRatio = getSint32(data, 44);
	riverDiameter = getSint32(data, 48);
	
	craterDensity = getSint32(data, 52);
	extraIslands = getSint32(data, 56);
	smooth=getSint32(data, 60);

	nbWorkers=getSint32(data, 64);
	nbTeams=getSint32(data, 68);
	
	oldIslandSize=getSint32(data, 72);
	oldBeach=getSint32(data, 76);
	
	fruitRatio = getSint32(data, 80);
	logRepeatAreaTimes = getSint32(data, 84);

	for (unsigned i=0; i<MAX_NB_RESOURCES; i++)
		resource[i]=getSint32(data, 88+i*4);

	bool good=true;
	if (getDataLength()!=dataLength)
		good=false;
	if (wDec>=32)
		good=false;
	if (hDec>=32)
		good=false;
	if (terrainType>GRASS)
		good=false;
	
	return (good);
}

void MapGenerationDescriptor::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("MapGenerationDescriptor");
	stream->write("MgdB", 4, "signatureStart");
	stream->write(getData(), DATA_SIZE, "data");
	stream->write("MgdE", 4, "signatureEnd");
	stream->writeLeaveSection();
}

bool MapGenerationDescriptor::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	stream->readEnterSection("MapGenerationDescriptor");
	char signature[4];
	stream->read(signature, 4, "signatureStart");
	if (memcmp(signature, "MgdB", 4) != 0)
	{
		stream->readLeaveSection();
		return false;
	}
	stream->read(getData(), DATA_SIZE, "data");
	setData(data, DATA_SIZE);
	stream->read(signature, 4, "signatureEnd");
	stream->readLeaveSection();
	if (memcmp(signature, "MgdE", 4) != 0)
		return false;
	return true;
}

Uint32 MapGenerationDescriptor::checkSum()
{
	Uint32 cs=0;
	
	cs^=wDec+(hDec<<16);
	cs^=(Sint32)terrainType;
	cs=rotr1(cs);
	cs^=(Sint32)method;
	cs=rotr1(cs);
	cs ^= waterRatio;
	cs=rotr1(cs);
	cs ^= sandRatio;
	cs=rotr1(cs);
	cs ^= grassRatio;
	cs=rotr1(cs);
	cs ^= desertRatio;
	cs=rotr1(cs);
	cs ^= wheatRatio;
	cs=rotr1(cs);
	cs ^= fruitRatio;
	cs=rotr1(cs);
	cs ^= woodRatio;
	cs=rotr1(cs);
	cs ^= algaeRatio;
	cs=rotr1(cs);
	cs ^= stoneRatio;
	cs=rotr1(cs);
	cs ^= riverDiameter;
	cs=rotr1(cs);
	cs ^= craterDensity;
	cs=rotr1(cs);
	cs ^= extraIslands;
	cs=rotr1(cs);
	cs ^= smooth;
	cs=rotr1(cs);
	cs ^= oldIslandSize;
	cs=rotr1(cs);
	cs ^= oldBeach;
	cs=rotr1(cs);
	cs ^= logRepeatAreaTimes;

	for (unsigned i=0; i<MAX_NB_RESOURCES; i++)
		cs+=resource[i]<<(3*i);

	cs=rotr1(cs);
	cs^=nbWorkers;
	cs^=nbTeams<<5;
	
	return cs;
}
