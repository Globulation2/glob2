/*
  AI-trainer dataset writer.

  Writes one record per executed Order to a binary file. Each record is
  a (state_blob, action) pair the trainer's BC pipeline consumes — the
  state_blob is the bot-team-only scalars + a fog-of-war-filtered 32×32×7
  spatial grid, computed at order time from the live Game state. See
  glob2-ai-trainer/docs/training-design.md §6 for the full rationale.

  Triggered by the GLOB2_DATASET_PATH env var, mirroring GLOB2_REPLAY_PATH
  and GLOB2_CHECKSUM_SIDECAR.

  GDS2 is little-endian. Header: magic[4], u32 record count, u32 metadata
  byte length, UTF-8 JSON metadata. Metadata is written on the first action,
  contains the exact catalog snapshot/hash, engine identity, and bounded model
  channel projection. Empty datasets have zero metadata bytes and records.

  Records: u32 tick, u8 sender, u8 order type, u32 state length, state bytes,
  u32 payload length, order payload bytes. State begins with u32 teams (=1),
  i32 prestige, u32 flags, 15 i32 resources, 3 i32 unit counts and 13 i32 bounded
  model building counts, then u32 grid width/height. Each grid cell is 9 bytes:
  u8 terrain, resource amount, own units, visible enemy units; u16 own building,
  visible enemy building; u8 discovery. Building values are concrete catalog
  IDs + 1; zero means absent. Terrain 255 means unrepresentable.

  Readers must branch on the magic. GDS1 has no metadata and its two building
  channels are u8 legacy families + 1 (7 bytes/cell). Never reinterpret those
  family numbers as GDS2 catalog IDs. The external trainer reader must add
  GDS2 support; old datasets remain readable through its GDS1 branch.
*/

#pragma once

#include <cstdio>
#include <string>
#include <vector>
#include "GAGSys.h"

class Order;
class Game;

class DatasetWriter
{
public:
	DatasetWriter();
	~DatasetWriter();

	/// Open the dataset file at `path`. Absolute paths are opened
	/// directly with fopen (matching ReplayWriter's bypass of the
	/// FileManager dirList prepend); relative paths go through the
	/// FileManager search dirs. Returns true on success.
	bool open(const std::string& path);

	bool isValid() const { return file != NULL; }

	/// Append one record. Called from Game::executeOrder for each order
	/// pushed through the engine. The state blob is computed from `game`
	/// using the sender's vision mask for fog-of-war filtering.
	void writeRecord(Uint32 tick, Order& order, Game& game);

	/// Patch num_records into the header and close the file.
	void close();

	/// Grid dimensions for the spatial channels of the state blob. The
	/// actual grid_w/grid_h written per record is min(map_w, GRID_W) /
	/// min(map_h, GRID_H), so smaller maps don't pad with empty cells.
	/// constexpr (rather than static const int) so std::min taking const&
	/// doesn't force an out-of-class definition.
	static constexpr int GRID_W = 32;
	static constexpr int GRID_H = 32;

private:
	FILE* file;
	Uint32 numRecords;
	const Game* catalogGame = nullptr;
	std::vector<int> modelChannels;

	void writeU16(Uint16 v);
	void writeU32(Uint32 v);
	void writeI32(Sint32 v);
	void writeStateBlob(int senderTeamNum, Game& game);
};

