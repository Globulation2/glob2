#include "ChecksumSidecar.h"
#include "FileFormatVersions.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Building.h"
#include <SDL_endian.h>
#include <cstring>
#include <vector>

ChecksumSidecarWriter::ChecksumSidecarWriter()
	: file(NULL), numTeams(0), numPlayers(0), ticksWritten(0)
{
}

ChecksumSidecarWriter::~ChecksumSidecarWriter()
{
	close();
}

// The on-disk sidecar format is canonically little-endian (see
// docs/replay-verification.md); the Rust reader in
// glob2-sim/src/cross_replay/sidecar.rs decodes with from_le_bytes.
// SDL_SwapLE* is a no-op on little-endian hosts, so sidecars written
// before this conversion existed remain valid there.
void ChecksumSidecarWriter::writeU16(Uint16 v)
{
	v = SDL_SwapLE16(v);
	fwrite(&v, sizeof(v), 1, file);
}

void ChecksumSidecarWriter::writeU32(Uint32 v)
{
	v = SDL_SwapLE32(v);
	fwrite(&v, sizeof(v), 1, file);
}

bool ChecksumSidecarWriter::open(const std::string& replayPath, int numTeams, int numPlayers)
{
	std::string path = replayPath + ".checksums";
	file = GAGCore::Toolkit::getFileManager()->openFP(path, "wb");
	if (!file)
		return false;

	this->numTeams = numTeams;
	this->numPlayers = numPlayers;
	ticksWritten = 0;

	// Header: magic + counts + placeholder for total_ticks + flags
	fwrite(FILE_SIG_CHECKSUM_SIDECAR, FILE_SIG_LEN, 1, file);
	writeU32(numTeams);
	writeU32(numPlayers);
	writeU32(0); // total_ticks placeholder
	writeU32(0); // flags

	return true;
}

void ChecksumSidecarWriter::writeTick(Uint32 tick, Uint32 totalChecksum, Game& game)
{
	if (!file)
		return;

	// Check optional max ticks limit
	const char* maxTicksEnv = getenv("GLOB2_CHECKSUM_SIDECAR_MAX_TICKS");
	if (maxTicksEnv)
	{
		Uint32 maxTicks = atoi(maxTicksEnv);
		if (maxTicks > 0 && ticksWritten >= maxTicks)
			return;
	}

	writeU32(tick);
	writeU32(totalChecksum);

	std::vector<Uint32> vec;

	for (int t = 0; t < numTeams; t++)
	{
		Team* team = game.teams[t];

		// Team-level checksum
		Uint32 teamCs = team->checkSum(NULL, NULL, NULL);
		writeU32(teamCs);

		// Units
		Uint32 unitCount = 0;
		for (int i = 0; i < Unit::MAX_COUNT; i++)
			if (team->myUnits[i])
				unitCount++;
		writeU32(unitCount);

		for (int i = 0; i < Unit::MAX_COUNT; i++)
		{
			if (!team->myUnits[i])
				continue;
			Unit* u = team->myUnits[i];
			vec.clear();
			Uint32 uCs = u->checkSum(&vec);
			writeU16((Uint16)u->gid);
			writeU32(uCs);
			writeU32((Uint32)vec.size());
			for (size_t j = 0; j < vec.size(); j++)
				writeU32(vec[j]);
		}

		// Buildings
		Uint32 bldgCount = 0;
		for (int i = 0; i < Building::MAX_COUNT; i++)
			if (team->myBuildings[i])
				bldgCount++;
		writeU32(bldgCount);

		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			if (!team->myBuildings[i])
				continue;
			Building* b = team->myBuildings[i];
			vec.clear();
			Uint32 bCs = b->checkSum(&vec);
			writeU16((Uint16)b->gid);
			writeU32(bCs);
			writeU32((Uint32)vec.size());
			for (size_t j = 0; j < vec.size(); j++)
				writeU32(vec[j]);
		}
	}

	ticksWritten++;
}

void ChecksumSidecarWriter::close()
{
	if (!file)
		return;

	// Patch total_ticks in header
	fseek(file, CHECKSUM_SIDECAR_TOTALTICKS_OFFSET, SEEK_SET);
	writeU32(ticksWritten);

	fclose(file);
	file = NULL;
}
