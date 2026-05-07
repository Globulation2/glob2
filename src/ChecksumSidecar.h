#pragma once

#include <string>
#include <cstdio>
#include <Toolkit.h>
#include <FileManager.h>
#include "GAGSys.h"

class Game;

class ChecksumSidecarWriter
{
public:
	ChecksumSidecarWriter();
	~ChecksumSidecarWriter();

	bool open(const std::string& replayPath, int numTeams, int numPlayers);
	void writeTick(Uint32 tick, Uint32 totalChecksum, Game& game);
	void close();

private:
	FILE* file;
	int numTeams;
	int numPlayers;
	Uint32 ticksWritten;

	void writeU16(Uint16 v);
	void writeU32(Uint32 v);
};

