/*
  AI-trainer dataset writer.

  Writes one record per executed Order to a binary file. Each record is
  the (state_blob, action) pair the trainer's BC pipeline consumes —
  state_blob is currently empty (format version 0; observation features
  land in a follow-up). The action half is the order type + payload
  exactly as it would be transmitted, plus the firing tick and sender.

  Triggered by the GLOB2_DATASET_PATH env var, mirroring GLOB2_REPLAY_PATH
  and GLOB2_CHECKSUM_SIDECAR.

  Binary format (little-endian, fixed-size where shown):

    HEADER (16 bytes)
      [4B] magic "GDS1"
      [4B] u32 format_version       (0; bump on schema change)
      [4B] u32 num_records          (patched on close())
      [4B] u32 flags                (reserved, 0)

    PER-RECORD
      [4B] u32 tick
      [1B] u8  sender_player_index
      [1B] u8  order_type
      [2B] u16 padding              (0)
      [4B] u32 state_blob_len       (0 in v0)
      [state_blob_len bytes]        state features (empty in v0)
      [4B] u32 order_payload_len
      [order_payload_len bytes]     order payload from Order::getData()
*/

#ifndef DATASET_WRITER_H
#define DATASET_WRITER_H

#include <cstdio>
#include <string>
#include "GAGSys.h"

class Order;

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
	/// pushed through the engine. The state blob is empty in format
	/// version 0; observation features will be added in a follow-up
	/// (then format_version bumps to 1).
	void writeRecord(Uint32 tick, Order& order);

	/// Patch num_records into the header and close the file.
	void close();

private:
	FILE* file;
	Uint32 numRecords;

	void writeU16(Uint16 v);
	void writeU32(Uint32 v);
};

#endif
