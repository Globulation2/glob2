// SPDX-License-Identifier: GPL-3.0-or-later
#include "LegacyGenerationDescriptor.h"
#include "GenerationContext.h"
#include "GeneratorRegistry.h"
#include "Marshaling.h"
#include "Utilities.h"
#include <Stream.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
using D = MapGenerationDescriptor;
namespace
{
Sint32 D::*legacyField(int method, const std::string &id)
{
	if (id == "water")
		return &D::waterRatio;
	if (id == "sand")
		return &D::sandRatio;
	if (id == "grass")
		return &D::grassRatio;
	if (id == "desert")
		return &D::desertRatio;
	if (id == "wheat")
		return &D::wheatRatio;
	if (id == "wood")
		return &D::woodRatio;
	if (id == "stone")
		return &D::stoneRatio;
	if (id == "algae")
		return &D::algaeRatio;
	if (id == "smoothing")
		return &D::smooth;
	if (id == "fruit")
		return &D::fruitRatio;
	if (id == "width")
		return &D::wDec;
	if (id == "height")
		return &D::hDec;
	if (id == "teams")
		return &D::nbTeams;
	if (id == "workers")
		return &D::nbWorkers;
	if (id == "river-width")
		return &D::riverDiameter;
	if (id == "lake-size")
		return &D::riverDiameter;
	if (id == "channel-width")
		return &D::riverDiameter;
	if (id == "bridge-width")
		return &D::riverDiameter;
	if (id == "lake-density")
		return &D::craterDensity;
	if (id == "extra-islands")
		return &D::extraIslands;
	if (id == "beach-size")
		return &D::oldBeach;
	if (id == "island-size")
		return method == D::eISLES ? &D::grassRatio : &D::oldIslandSize;
	// "repeat" and every option introduced by generators added after the
	// fixed-size legacy descriptor was frozen (loopiness, home-radius,
	// cell-size, room-size, corridor-width, islet-size, bridge-count,
	// commons-size, home-island-size, moat-width, continent-size,
	// coast-roughness, resource-islands, fjord-width, ...) have no slot in
	// it. Callers fall back to logRepeatAreaTimes for "repeat" specifically
	// and otherwise treat a null field as "not representable", rather than
	// throwing and taking down any caller that converts a newer generator's
	// full option set through this adapter.
	return nullptr;
}
} // namespace
int GeneratorControl::get(const D &d) const
{
	auto field = legacyField(d.method, id);
	if (field)
		return d.*field;
	return id == "repeat" ? d.logRepeatAreaTimes : defaultValue;
}
void GeneratorControl::set(D &d, int v) const
{
	auto field = legacyField(d.method, id);
	if (field)
		d.*field = normalize(v);
	else if (id == "repeat")
		d.logRepeatAreaTimes = normalize(v);
	// Else: no legacy field for this option: nothing to store.
}
const std::vector<D::Control> &D::controls(Method m) { return GenerationRequest::controls(m); }
const std::vector<D::Control> &D::sharedControls() { return sharedGeneratorControls(); }
const D::Control &D::control(Method m, const char *label)
{
	for (const auto &c : controls(m))
		if (std::strcmp(c.label, label) == 0)
			return c;
	for (const auto &c : sharedControls())
		if (std::strcmp(c.label, label) == 0)
			return c;
	throw std::invalid_argument("Unknown legacy control");
}
const char *D::methodName(Method m) { return GenerationRequest::methodName(m); }
D::MapGenerationDescriptor()
{
	terrainType = GRASS;
	for (const auto &c : sharedControls())
		c.set(*this, c.defaultValue);
	setMethodDefaults(eUNIFORM);
	std::fill(std::begin(resource), std::end(resource), 7);
}
D::~MapGenerationDescriptor() = default;
void D::setMethodDefaults(Method m)
{
	method = m;
	waterRatio = grassRatio = 50;
	sandRatio = desertRatio = 0;
	smooth = 4;
	riverDiameter = craterDensity = 50;
	oldIslandSize = 65;
	oldBeach = 1;
	fruitRatio = 4;
	extraIslands = 0;
	logRepeatAreaTimes = 0;
	wheatRatio = woodRatio = stoneRatio = algaeRatio = 50;
	for (const auto &c : controls(m))
		c.set(*this, c.defaultValue);
}
bool D::hasTerrainWeight() const { return fromLegacyDescriptor(*this, 0).hasTerrainWeight(); }
GenerationRequest fromLegacyDescriptor(const D &d, std::uint32_t seed)
{
	GenerationRequest r;
	r.method = d.method;
	r.seed = seed;
	r.terrainType = d.terrainType;
	r.wDec = d.wDec;
	r.hDec = d.hDec;
	r.nbTeams = d.nbTeams;
	r.nbWorkers = d.nbWorkers;
	if (!GeneratorRegistry::builtins().find(d.method))
		return r;
	for (const auto &c : D::controls(d.method))
	{
		int value = c.get(d);
		if (d.riverDiameter == 50)
		{
			if (c.id == "lake-size")
				value = 30;
			if (c.id == "channel-width")
				value = 6;
			if (c.id == "bridge-width")
				value = 4;
		}
		// Historical negative neutral count requested a random count. Resolve at
		// the adapter boundary from the explicit seed, within the supported range.
		if (c.id == "extra-islands" && d.method == D::eCONCRETEISLANDS && value < 0)
			value =
				int(GenerationContext::deriveSeed(seed, "legacy-neutral-count") % (c.maximum + 1));
		r.options[c.id] = value;
	}
	std::copy(std::begin(d.resource), std::end(d.resource), r.resourceAmounts.begin());
	return r;
}
D toLegacyDescriptor(const GenerationRequest &r)
{
	D d;
	d.setMethodDefaults(static_cast<D::Method>(r.method));
	d.terrainType = r.terrainType;
	for (const auto &c : D::sharedControls())
		c.set(d, c.get(r));
	for (const auto &c : D::controls(d.method))
		c.set(d, c.get(r));
	std::copy(r.resourceAmounts.begin(), r.resourceAmounts.end(), d.resource);
	return d;
}
Uint8 *MapGenerationDescriptor::getData()
{
	assert(DATA_SIZE == 100 + MAX_NB_RESOURCES * 4);

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

	for (unsigned i = 0; i < MAX_NB_RESOURCES; i++)
		addSint32(data, resource[i], 88 + i * 4);

	return data;
}

bool MapGenerationDescriptor::setData(const Uint8 *data, int dataLength)
{
	assert(DATA_SIZE == 100 + MAX_NB_RESOURCES * 4);
	assert(getDataLength() == DATA_SIZE);
	assert(getDataLength() == dataLength);

	wDec = getSint32(data, 0);
	hDec = getSint32(data, 4);

	terrainType = (TerrainType)getSint32(data, 8);

	method = (Method)getSint32(data, 12);
	waterRatio = getSint32(data, 16);
	sandRatio = getSint32(data, 20);
	grassRatio = getSint32(data, 24);
	desertRatio = getSint32(data, 28);

	wheatRatio = getSint32(data, 32);
	woodRatio = getSint32(data, 36);
	algaeRatio = getSint32(data, 40);
	stoneRatio = getSint32(data, 44);
	riverDiameter = getSint32(data, 48);

	craterDensity = getSint32(data, 52);
	extraIslands = getSint32(data, 56);
	smooth = getSint32(data, 60);

	nbWorkers = getSint32(data, 64);
	nbTeams = getSint32(data, 68);

	oldIslandSize = getSint32(data, 72);
	oldBeach = getSint32(data, 76);

	fruitRatio = getSint32(data, 80);
	logRepeatAreaTimes = getSint32(data, 84);

	for (unsigned i = 0; i < MAX_NB_RESOURCES; i++)
		resource[i] = getSint32(data, 88 + i * 4);

	bool good = true;
	if (getDataLength() != dataLength)
		good = false;
	if (wDec >= 32)
		good = false;
	if (hDec >= 32)
		good = false;
	if (terrainType > GRASS)
		good = false;

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
	Uint32 cs = 0;

	cs ^= wDec + (hDec << 16);
	cs ^= (Sint32)terrainType;
	cs = rotr1(cs);
	cs ^= (Sint32)method;
	cs = rotr1(cs);
	cs ^= waterRatio;
	cs = rotr1(cs);
	cs ^= sandRatio;
	cs = rotr1(cs);
	cs ^= grassRatio;
	cs = rotr1(cs);
	cs ^= desertRatio;
	cs = rotr1(cs);
	cs ^= wheatRatio;
	cs = rotr1(cs);
	cs ^= fruitRatio;
	cs = rotr1(cs);
	cs ^= woodRatio;
	cs = rotr1(cs);
	cs ^= algaeRatio;
	cs = rotr1(cs);
	cs ^= stoneRatio;
	cs = rotr1(cs);
	cs ^= riverDiameter;
	cs = rotr1(cs);
	cs ^= craterDensity;
	cs = rotr1(cs);
	cs ^= extraIslands;
	cs = rotr1(cs);
	cs ^= smooth;
	cs = rotr1(cs);
	cs ^= oldIslandSize;
	cs = rotr1(cs);
	cs ^= oldBeach;
	cs = rotr1(cs);
	cs ^= logRepeatAreaTimes;

	for (unsigned i = 0; i < MAX_NB_RESOURCES; i++)
		cs += static_cast<Uint32>(resource[i]) << ((3 * i) % 32);

	cs = rotr1(cs);
	cs ^= nbWorkers;
	cs ^= nbTeams << 5;

	return cs;
}
