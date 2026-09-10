// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "Ressource.h"
#include "TerrainType.h"
#include "Team.h"
#include <array>
#include <vector>

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

class MapGenerationDescriptor
{
public:
	MapGenerationDescriptor();
	virtual ~MapGenerationDescriptor(void);
	
	Uint8 *getData();
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength() {return DATA_SIZE; }
	
	void save(GAGCore::OutputStream *stream);
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	Uint32 checkSum();

public:
	TerrainType terrainType;
	enum Method
	{
		/// No terrain (terrain undefined)
		eNONE=-1,
		/// Uniform terrain (all of one type. completely unstructured)
		eUNIFORM=0,
		/// swamp-like terrain with water here and land there
		eSWAMP=1,
		/// a more or less winding river
		eRIVER=2,
		/// islands that have organic shape and no passage from one to the next
		eISLANDS=3,
		/// all connected land with round lakes
		eCRATERLAKES=4,
		eCONCRETEISLANDS=5,
		eISLES=6,
		eOLDRANDOM=7,
		eOLDISLANDS=8
	};
	static constexpr int METHOD_COUNT = eOLDISLANDS + 1;
	/// Set this generator's terrain controls without changing map size or teams.
	void setMethodDefaults(Method newMethod);

	// UI-independent definitions shared by the lobby, editor, presets and validation.
	enum class ControlGroup
	{
		Terrain,
		Resources,
		Layout,
		Shared
	};
	struct Control
	{
		const char *label;
		Sint32 MapGenerationDescriptor::*field; // null only for unsigned repeat exponent
		int minimum, maximum, step, defaultValue;
		ControlGroup group = ControlGroup::Terrain;
		bool powerOfTwo = false;
		int get(const MapGenerationDescriptor &descriptor) const;
		void set(MapGenerationDescriptor &descriptor, int value) const;
		int normalize(int value) const;
	};
	static const std::vector<Control> &controls(Method method);
	static const std::vector<Control> &sharedControls();
	static const Control &control(Method method, const char *label);
	static const char *methodName(Method method);
	bool hasTerrainWeight() const;

	Method method;
	
	Sint32 wDec, hDec;
	
	Sint32 waterRatio, sandRatio, grassRatio, desertRatio, wheatRatio,
		woodRatio, fruitRatio, algaeRatio, stoneRatio, riverDiameter, craterDensity, extraIslands;
	Sint32 oldIslandSize, oldBeach;
	// Mode-specific reuse keeps the existing serialized layout:
	// riverDiameter = river diameter / lake size / channel width / bridge width;
	// grassRatio = island size for Isles; extraIslands = neutral count for Concrete Islands.
	Sint32 smooth;
	Sint32 resource[MAX_NB_RESOURCES];
	///n=2^n-times the same landscape. So 0=all random.
	Uint32 logRepeatAreaTimes;

	Sint32 nbTeams, nbWorkers;
public:
	// Those may not be in data
  Sint32 bootX[Team::MAX_COUNT]{};
  Sint32 bootY[Team::MAX_COUNT]{};

public:
	enum {DATA_SIZE=100+MAX_NB_RESOURCES*4};
protected:
	//! Serialized form of MapGenerationDescriptor
  Uint8 data[DATA_SIZE]{};
};

// Remember terrain edits per method while sharing map size, teams and workers.
class MapGenerationHistory
{
	std::array<MapGenerationDescriptor, MapGenerationDescriptor::METHOD_COUNT> settings;

  public:
	MapGenerationHistory();
	void select(MapGenerationDescriptor &current, MapGenerationDescriptor::Method method);
};
