// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2010 Michiel De Muynck

#pragma once

#include <memory>
#include <assert.h>
#include "Types.h"

namespace GAGCore
{
	class OutputStream;
	class StreamBackend;
}

class GameGUI;
class Order;

/// This class is used for writing replays.
/// ReplayWriter buffers a whole replay and then writes it to a file when you call write()
/// You can optionally (though preferably) write checksums that will then be checked when reading back the replay.
class ReplayWriter
{
public:
	/// Constructs the replay writer
	ReplayWriter();

	/// Destructor
	~ReplayWriter();

	/// Initialise the writer with the given Map- and GameHeader.
	/// The first argument is the backend that will be used.
	/// If it is "", the buffer will be in memory, otherwise it's a file with the given filename.
	void init(const std::string &backend, GameGUI &gui);

	/// Returns false if the writer isn't initialised or if the data does not make sense
	bool isValid() const;

	/// Increments the current step number and updates the checksum.
	void advanceStep();

	/// If the checksum is 0, there won't be any checking if the checksums match for orders.
	void setCheckSum(Uint32 checksum = 0);

	/// Adds the order to the replay
	void pushOrder(std::shared_ptr<Order> order);

	/// Marks the end of the replay
	void finish();

	/// Write this replay to the given file
	/// Returns true if successful
	bool write(const std::string &filename) const;

	/// Get the buffer, if for any reason you would need it
	GAGCore::OutputStream* getBuffer() const;

	/// Number of orders pushed into the replay (excluding ORDER_VOICE_DATA
	/// and ORDER_NULL, matching the actual write criteria in pushOrder).
	/// Used by the AI-trainer pipeline for sidecar metadata.
	Uint32 getOrderCount() const { return ordersWritten; }

private:
	/// You shouldn't copy-construct this class
	ReplayWriter(const ReplayWriter &copy) { assert(false); };

	/// You shouldn't use assignment on this class
	void operator=(const ReplayWriter &writer) { assert(false); };

	/// The StreamBackend of the buffer
	GAGCore::StreamBackend *bufferBackend;

	/// The buffer OutputStream
	GAGCore::OutputStream *buffer;

	/// The number of steps since the last order
	Uint32 stepsSinceLastOrder;

	/// The game's current checksum (or 0 if it's not given)
	Uint32 checksum;

	/// Counter of orders actually written to the buffer. Excludes voice and
	/// null orders (matching the early-return in pushOrder).
	Uint32 ordersWritten;
};

