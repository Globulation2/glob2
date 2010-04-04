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

#ifndef __ReplayWriter_h
#define __ReplayWriter_h

#include <boost/shared_ptr.hpp>
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

	/// Increments the current step number
	void advanceStep();

	/// Adds the order to the replay
	void pushOrder(boost::shared_ptr<Order> order);

	/// Marks the end of the replay
	void finish();

	/// Write this replay to the given file
	/// Returns true if successful
	bool write(const std::string &filename) const;

	/// Get the buffer, if for any reason you would need it
	GAGCore::OutputStream* getBuffer() const;

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
	Uint16 stepsSinceLastOrder;
};

#endif
