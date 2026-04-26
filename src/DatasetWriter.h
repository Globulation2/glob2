/*
  AI-trainer dataset writer.

  Writes one record per executed Order to a binary file. Each record is
  the (state_blob, action) pair the trainer's BC pipeline consumes —
  state_blob is currently empty; observation-feature serialization lands
  in a follow-up alongside the trainer-side training loop.

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
      [4B] u32 state_blob_len       (currently 0)
      [state_blob_len bytes]        state features (currently empty)
      [4B] u32 order_payload_len
      [order_payload_len bytes]     order payload from Order::getData()

  No version field: there's a single producer (this writer) and a single
  consumer (the trainer's `dataset.rs` parser), regenerating datasets is
  cheap, and we'd never need to support multiple wire formats in flight.
  If the schema ever changes wire-incompatibly, bump the magic to "GDS2"
  and parsers reject by magic mismatch.
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
	/// pushed through the engine. The state blob is currently empty
	/// (length-prefixed 0); observation features get added in a
	/// follow-up alongside the trainer-side training loop.
	void writeRecord(Uint32 tick, Order& order);

	/// Patch num_records into the header and close the file.
	void close();

private:
	FILE* file;
	Uint32 numRecords;

	void writeU32(Uint32 v);
};

#endif
