// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "GraphicContext.h"

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

class BaseTeam
{
public:
	enum TeamType
	{
		T_HUMAN,
		T_AI,
		// Note : T_AI + n is AI type n
	};

	BaseTeam();
	virtual ~BaseTeam(void) { }

	TeamType type;
	Sint32 teamNumber; // index of the current team in the game::teams[] array.
	Sint32 numberOfPlayer; // number of controlling players
	GAGCore::Color color;
	Uint32 playersMask;
	
public:
	bool disableRecursiveDestruction;

public:
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream) const;

	Uint32 checkSum();
};

