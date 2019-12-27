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

#include "ReplayWriter.h"

#include "BinaryStream.h"
#include "StreamBackend.h"
#include "Order.h"
#include "NetMessage.h"
#include "GameGUI.h"
#include "Version.h"
#include "Toolkit.h"
#include "FileManager.h"

#include <stdio.h>

// Write an Order to the stream, with the given checksum
inline void writeOrder(GAGCore::OutputStream *stream, boost::shared_ptr<Order> order, Uint32 checksum = 0)
{
	// Write the checksum
	order->gameCheckSum = checksum;

	// A NetSendOrder has methods to write an Order to a stream
	NetSendOrder msg(order);

	// Write the data of the Order to the file
	msg.encodeData(stream);
}

ReplayWriter::ReplayWriter()
{
	bufferBackend = NULL;
	buffer = NULL;
	stepsSinceLastOrder = 0;
	checksum = 0;
}

ReplayWriter::~ReplayWriter()
{
	finish();

	delete buffer;
}

void ReplayWriter::init(const std::string &backend, GameGUI &gui)
{
	// Avoid trouble
	checksum = 0;

	// Initialise the buffer backend
	if (backend == "")
	{
		bufferBackend = new MemoryStreamBackend();
	}
	else
	{
		FILE* fp = Toolkit::getFileManager()->openFP(backend, "w+");
		bufferBackend = new FileStreamBackend(fp);
	}

	// Initialise the buffer OutputStream
	assert(bufferBackend->isValid());
	buffer = new BinaryOutputStream(bufferBackend);
	assert(buffer->isValid());

	// Write the game's header to the buffer
	gui.save(buffer, "replayHeader");

	// Write the current glob2 version number to the buffer
	buffer->writeUint16(VERSION_MAJOR, "versionMajor");
	buffer->writeUint16(VERSION_MINOR, "versionMinor");
}

bool ReplayWriter::isValid() const
{
	return (buffer != NULL && bufferBackend != NULL && buffer->isValid());
}

void ReplayWriter::advanceStep()
{
	stepsSinceLastOrder++;
}

void ReplayWriter::setCheckSum(Uint32 checksum)
{
	this->checksum = checksum;
}

void ReplayWriter::pushOrder(boost::shared_ptr<Order> order)
{
	if (!isValid()) return;
	if (order->getOrderType() == ORDER_VOICE_DATA || order->getOrderType() == ORDER_NULL) return;

	// Write the number of steps since last order to this order (can be 0)
	buffer->writeUint16(stepsSinceLastOrder, "replayStepsSinceLastOrder");

	// Write the Order to the file
	writeOrder(buffer, order, checksum);

	stepsSinceLastOrder = 0;

	// Don't flush the buffer. That is done when writing the last Order, in ReplayWriter::finish().
}

void ReplayWriter::finish()
{
	if (!isValid()) return;

	// Write the number of steps since last order to the end of the replay
	buffer->writeUint16(stepsSinceLastOrder, "replayStepsSinceLastOrder");

	// We write a NullOrder to mark the end of the replay (like terminating a string with \0)
	writeOrder(buffer, boost::shared_ptr<Order>(new NullOrder()), 0);

	// Flush the buffer now
	buffer->flush();

	stepsSinceLastOrder = 0;
}

bool ReplayWriter::write(const std::string &filename) const
{
	if (!isValid()) return false;
	if (filename == "") return false;

	// Make sure the buffer is flushed
	buffer->flush();

	// Open the file as a backend
	StreamBackend* fileBackend = Toolkit::getFileManager()->openOutputStreamBackend(filename);
	assert(fileBackend->isValid());

	// Open the file as an OutputStream
	OutputStream* file = new BinaryOutputStream(fileBackend);

	// Save the current position in the buffer
	size_t pos = bufferBackend->getPosition();

	// Go back to the beginning of the buffer
	bufferBackend->seekFromStart(0);

	// Copy the buffer to the file
	while (!bufferBackend->isEndOfStream())
	{
		int c = bufferBackend->getChar();
		if (bufferBackend->isEndOfStream()) break;
		fileBackend->putc(c);
	}

	// Write the number of steps since last order to the end of the replay
	file->writeUint16(0, "replayStepsSinceLastOrder");

	// Write a NullOrder to the file to make sure it's a NullOrder-terminated replay
	writeOrder(file, boost::shared_ptr<Order>(new NullOrder()), 0);

	// Flush the file
	file->flush();
	delete file;

	// Go back to the right position in the buffer
	buffer->seekFromStart(pos);

	return true;
}

GAGCore::OutputStream* ReplayWriter::getBuffer() const
{
	return buffer;
}
