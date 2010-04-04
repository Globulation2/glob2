/*
  Copyright (C) 2010 Michiel De Muynck

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __ReplayReader_h
#define __ReplayReader_h

#include <boost/shared_ptr.hpp>
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

	/// Get the next order on the current step
	boost::shared_ptr<Order> retrieveOrder();

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
};

#endif
