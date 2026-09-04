// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2010 Michiel De Muynck

#ifndef __ReplayReader_h
#define __ReplayReader_h

#include <memory>
#include <assert.h>
#include <string>
#include "Types.h"

namespace GAGCore
{
	class InputStream;
}

class Order;

/// This class is used for reading replays.
/// The replay stream is kept open and read every time you do retrieveOrder.
/// If this replay stores checksums, they are checked every time an order is read.
/// The replay ends early if both the checksum given to this class by setCheckSum(checksum) != 0
/// AND the order written in the replay file != 0 AND both don't match.
class ReplayReader
{
public:
	/// Constructs the reader, a stream will have to be loaded later with load() before the reader can be used
	ReplayReader();

	/// Destructor
	~ReplayReader();

	/// Loads the replay in the given InputStream.
	/// If skipToOrders is true, it read the header in the stream and discard the data to jump to the place where the orders start.
	/// If skipToOrders is false, it will assume the stream has already been used to read the header (using GameGUI::load()).
	/// Returns true if successful.
	///  ! IMPORTANT: It owns the stream afterwards. It will delete the stream when it's no longer used.
	bool loadReplay(GAGCore::InputStream *stream, bool skipToOrders);

	/// Loads the replay with the given filename. Returns true if successful.
	bool loadReplay(const std::string &filename);

	/// Returns false if the stream is not initialized or the internal data does not make sense
	bool isValid() const;

	/// Returns true if there are still more orders on this step
	bool hasMoreOrdersThisStep() const;

	/// Returns the current step number
	Uint32 getCurrentStep() const;

	/// Returns the total number of steps
	Uint32 getNumStepsTotal() const;

	/// Returns true if the replay has finished
	bool isFinished() const;

	/// Increments the current step number
	void advanceStep();

	/// Set the checksum. 0 means no testing is done for checksum matches.
	void setCheckSum(Uint32 checksum = 0);

	/// Get the next order on the current step
	std::shared_ptr<Order> retrieveOrder();

	/// Get the stream that this reader uses, or NULL if there is none
	GAGCore::InputStream *getStream() const;

private:
	/// You shouldn't copy-construct this class
	ReplayReader(const ReplayReader &copy) { assert(false); };

	/// You shouldn't use assignment on this class
	void operator=(const ReplayReader &reader) { assert(false); };

	/// The stream it reads the replay from
	GAGCore::InputStream *stream;

	/// Current step number
	Uint32 currentStep;

	/// The total amount of steps the replay
	Uint32 numSteps;

	/// The amount of orders already processed
	Uint32 ordersProcessed;

	/// The total amount of orders the replay
	Uint32 numOrders;

	/// The number of steps until the next order
	Uint16 stepsUntilNextOrder;

	/// The game's current checksum (or 0 if it's not given)
	Uint32 checksum;
};

#endif
