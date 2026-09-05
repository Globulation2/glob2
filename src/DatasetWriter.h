/*
  AI-trainer dataset writer.

  Writes one record per executed Order to a binary file. Each record is
  a (state_blob, action) pair the trainer's BC pipeline consumes — the
  state_blob is the bot-team-only scalars + a fog-of-war-filtered 32×32×7
  spatial grid, computed at order time from the live Game state. See
  glob2-ai-trainer/docs/training-design.md §6 for the full rationale.

  Triggered by the GLOB2_DATASET_PATH env var, mirroring GLOB2_REPLAY_PATH
  and GLOB2_CHECKSUM_SIDECAR.

  Binary format (little-endian, fixed-size where shown):

    HEADER (8 bytes)
      [4B] magic "GDS1"
      [4B] u32 num_records          (patched on close())

    PER-RECORD
      [4B] u32 tick
      [1B] u8  sender_player_index
      [1B] u8  order_type
      [4B] u32 state_blob_len
      [state_blob_len bytes]        observation features (see layout below)
      [4B] u32 order_payload_len
      [order_payload_len bytes]     order payload from Order::getData()

  STATE BLOB (variable size; ~7.3 KB at GRID_W=GRID_H=32)
    [4B] u32 num_teams              (always 1 — bot-team-only by design)
    per team:
      [4B] i32 prestige
      [4B] u32 flags                (bit0=isAlive, bit1=hasWon, bit2=hasLost)
      [4B × 15] i32 teamRessources  (MAX_NB_RESSOURCES)
      [4B × 3]  i32 unit_count_by_type     (WORKER, EXPLORER, WARRIOR)
      [4B × 13] i32 building_count_by_type (NB_BUILDING)
    [4B] u32 grid_w                 (≤32, == min(map_w, 32))
    [4B] u32 grid_h                 (≤32, == min(map_h, 32))
    per cell × 7 channels (HWC; row-major, gy outer, gx inner):
      [1B] terrain                  (0=GRASS, 1=SAND, 2=WATER, 255=other)
      [1B] resource_amount          (sum across cell, capped at 255; FOW: visible only)
      [1B] my_unit_count            (capped at 255; always shown — units are mine)
      [1B] enemy_unit_count         (capped at 255; FOW: visible only)
      [1B] my_building_type         (0=none, 1..NB_BUILDING; always shown)
      [1B] enemy_building_type      (0=none, 1..NB_BUILDING; FOW: visible only)
      [1B] discovery                (0=unknown, 1=previously seen, 2=currently visible)

  No version field: there's a single producer (this writer) and a single
  consumer (the trainer's `dataset.rs` parser), regenerating datasets is
  cheap, and we'd never need to support multiple wire formats in flight.
  If the schema ever changes wire-incompatibly, bump the magic to "GDS2"
  and parsers reject by magic mismatch.
*/

#pragma once

#include <cstdio>
#include <string>
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

	void writeU32(Uint32 v);
	void writeI32(Sint32 v);
	void writeStateBlob(int senderTeamNum, Game& game);
};

