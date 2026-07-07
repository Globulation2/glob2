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

