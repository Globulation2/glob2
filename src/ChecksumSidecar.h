#pragma once

#include <string>
#include <cstdio>
#include <Toolkit.h>
#include <FileManager.h>
#include "GAGSys.h"

class Game;

//! Byte offset of the `total_ticks` Uint32 inside the sidecar header.
//! All multi-byte integers in the sidecar are little-endian on disk.
//! Header layout:
//!   [0..3]   FILE_SIG_CHECKSUM_SIDECAR (4-byte ASCII magic)
//!   [4..7]   numTeams (Uint32)
//!   [8..11]  numPlayers (Uint32)
//!   [12..15] total_ticks (Uint32)        <-- this offset
//!   [16..19] flags (Uint32, reserved)
//! Patched at close() once the final tick count is known. If the header
//! layout changes, this offset must change too. See ChecksumSidecar.cpp.
static constexpr long CHECKSUM_SIDECAR_TOTALTICKS_OFFSET = 12;

//! Streams per-tick simulation checksums to `<replayPath>.checksums`.
//!
//! Lifecycle contract: open(game) once, then writeTick(..., game) with the
//! SAME game object every tick, then close(). The writer is single-use — to
//! record a different game, destroy it and construct a new one; there is no
//! reopen. The team/player counts in the header are read from `game` at
//! open(), and writeTick() re-reads the live counts from `game` each tick,
//! so the two can only disagree if the counts change mid-game (the engine
//! never does this: Game::teamsCount() comes from the immutable MapHeader).
class ChecksumSidecarWriter
{
public:
	ChecksumSidecarWriter();
	~ChecksumSidecarWriter();

	bool open(const std::string& replayPath, const Game& game);
	void writeTick(Uint32 tick, Uint32 totalChecksum, Game& game);
	//! Patches total_ticks into the header and closes the file. Returns
	//! false if any write, seek, or close failed since open(); in that case
	//! the on-disk file has been deleted, because a truncated sidecar would
	//! otherwise parse as a shorter-but-valid run and be silently treated
	//! as authoritative by the cross-replay verification tooling.
	bool close();

private:
	FILE* file;
	std::string path;
	Uint32 ticksWritten;
	//! Sticky success flag: cleared by the first failed write/seek/close and
	//! never set again until the next open(). Once cleared, all further
	//! writes are no-ops so we don't keep hammering a broken stream.
	bool ok;

	void writeBytes(const void* data, size_t size);
	void writeU16(Uint16 v);
	void writeU32(Uint32 v);
};

