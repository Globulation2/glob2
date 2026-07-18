#include "ChecksumSidecar.h"
#include "FileFormatVersions.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Building.h"
#include <SDL_endian.h>
#include <cassert>
#include <cstring>
#include <vector>

ChecksumSidecarWriter::ChecksumSidecarWriter()
	: file(NULL), ticksWritten(0), ok(true)
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
// All writes funnel through here so a single short write flips the sticky
// `ok` flag and turns every subsequent write into a no-op (disk full, EIO,
// quota — there is no point writing more of a record that is already broken).
void ChecksumSidecarWriter::writeBytes(const void* data, size_t size)
{
	if (!ok)
		return;
	if (fwrite(data, size, 1, file) != 1)
		ok = false;
}

void ChecksumSidecarWriter::writeU16(Uint16 v)
{
	v = SDL_SwapLE16(v);
	writeBytes(&v, sizeof(v));
}

void ChecksumSidecarWriter::writeU32(Uint32 v)
{
	v = SDL_SwapLE32(v);
	writeBytes(&v, sizeof(v));
}

bool ChecksumSidecarWriter::open(const std::string& replayPath, const Game& game)
{
	const int numTeams = game.teamsCount();
	const int numPlayers = game.gameHeader.getNumberOfPlayers();
	// A count outside the live array bounds would make writeTick index past
	// game.teams[] — refuse to open rather than emit a sidecar whose header
	// promises data we could never write.
	if (numTeams < 1 || numTeams > Team::MAX_COUNT
		|| numPlayers < 1 || numPlayers > Team::MAX_COUNT)
		return false;

	path = replayPath + ".checksums";
	file = GAGCore::Toolkit::getFileManager()->openFP(path, "wb");
	if (!file)
		return false;

	ticksWritten = 0;
	ok = true;

	// Header: magic + counts + placeholder for total_ticks + flags
	writeBytes(FILE_SIG_CHECKSUM_SIDECAR, FILE_SIG_LEN);
	writeU32(numTeams);
	writeU32(numPlayers);
	writeU32(0); // total_ticks placeholder
	writeU32(0); // flags

	if (!ok)
	{
		// Could not even write the 20-byte header: close and delete the
		// stub so no reader ever sees a headerless/partial sidecar.
		fclose(file);
		file = NULL;
		GAGCore::Toolkit::getFileManager()->remove(path);
		return false;
	}
	return true;
}

void ChecksumSidecarWriter::writeTick(Uint32 tick, Uint32 totalChecksum, Game& game)
{
	if (!file || !ok)
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

	// Counts come from the live game, not from state captured at open() —
	// so a stale writer can never index past game.teams[]. teamsCount() is
	// fixed for the whole game (MapHeader), so this always matches the
	// header written by open().
	const int numTeams = game.teamsCount();
	assert(numTeams >= 1 && numTeams <= Team::MAX_COUNT);

	for (int t = 0; t < numTeams; t++)
	{
		Team* team = game.teams[t];
		assert(team);

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

bool ChecksumSidecarWriter::close()
{
	if (!file)
		return ok;

	// Patch total_ticks in header
	if (fseek(file, CHECKSUM_SIDECAR_TOTALTICKS_OFFSET, SEEK_SET) != 0)
		ok = false;
	else
		writeU32(ticksWritten);

	// fclose flushes buffered data, so its return catches deferred write
	// errors (e.g. ENOSPC surfacing only at flush time).
	if (fclose(file) != 0)
		ok = false;
	file = NULL;

	if (!ok)
	{
		// A truncated sidecar parses as a valid shorter run on the reader
		// side — delete it so it can never be mistaken for authoritative.
		GAGCore::Toolkit::getFileManager()->remove(path);
	}
	return ok;
}
