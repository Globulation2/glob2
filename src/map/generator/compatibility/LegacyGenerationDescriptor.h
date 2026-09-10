// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "GenerationRequest.h"
#include "GeneratorControls.h"
#include "Ressource.h"
#include "Team.h"
#include "TerrainType.h"
#include <array>
#include <vector>

namespace GAGCore
{
class InputStream;
class OutputStream;
} // namespace GAGCore

class MapGenerationDescriptor
{
  public:
	MapGenerationDescriptor();
	virtual ~MapGenerationDescriptor(void);

	Uint8 *getData();
	bool setData(const Uint8 *data, int dataLength);
	int getDataLength() { return DATA_SIZE; }

	void save(GAGCore::OutputStream *stream);
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	Uint32 checkSum();

  public:
	TerrainType terrainType;
	using Method = GenerationRequest::Method;
	static constexpr Method eNONE = GenerationRequest::eNONE;
	static constexpr Method eUNIFORM = GenerationRequest::eUNIFORM;
	static constexpr Method eSWAMP = GenerationRequest::eSWAMP;
	static constexpr Method eRIVER = GenerationRequest::eRIVER;
	static constexpr Method eISLANDS = GenerationRequest::eISLANDS;
	static constexpr Method eCRATERLAKES = GenerationRequest::eCRATERLAKES;
	static constexpr Method eCONCRETEISLANDS = GenerationRequest::eCONCRETEISLANDS;
	static constexpr Method eISLES = GenerationRequest::eISLES;
	static constexpr Method eOLDRANDOM = GenerationRequest::eOLDRANDOM;
	static constexpr Method eOLDISLANDS = GenerationRequest::eOLDISLANDS;
	static constexpr int METHOD_COUNT = eOLDISLANDS + 1;
	/// Set this generator's terrain controls without changing map size or teams.
	void setMethodDefaults(Method newMethod);

	using Control = GeneratorControl;
	using ControlGroup = ::ControlGroup;
	static const std::vector<Control> &controls(Method);
	static const std::vector<Control> &sharedControls();
	static const Control &control(Method, const char *label);
	static const char *methodName(Method);
	bool hasTerrainWeight() const;
	Method method;

	Sint32 wDec, hDec;

	Sint32 waterRatio, sandRatio, grassRatio, desertRatio, wheatRatio, woodRatio, fruitRatio,
		algaeRatio, stoneRatio, riverDiameter, craterDensity, extraIslands;
	Sint32 oldIslandSize, oldBeach;
	// Mode-specific reuse keeps the existing serialized layout:
	// riverDiameter = river diameter / lake size / channel width / bridge width;
	// grassRatio = island size for Isles; extraIslands = neutral count for Concrete Islands.
	Sint32 smooth;
	Sint32 resource[MAX_NB_RESOURCES];
	/// n=2^n-times the same landscape. So 0=all random.
	Uint32 logRepeatAreaTimes;

	Sint32 nbTeams, nbWorkers;

  public:
	// Those may not be in data
	Sint32 bootX[Team::MAX_COUNT]{};
	Sint32 bootY[Team::MAX_COUNT]{};

  public:
	enum
	{
		DATA_SIZE = 100 + MAX_NB_RESOURCES * 4
	};

  protected:
	//! Serialized form of MapGenerationDescriptor
	Uint8 data[DATA_SIZE]{};
};

GenerationRequest fromLegacyDescriptor(const MapGenerationDescriptor &, std::uint32_t seed);
MapGenerationDescriptor toLegacyDescriptor(const GenerationRequest &);
