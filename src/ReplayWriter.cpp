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

#include "Stream.h"
#include "StreamBackend.h"
#include "Order.h"
#include "NetMessage.h"
#include "GameGUI.h"
#include "Version.h"
#include "Toolkit.h"
#include "FileManager.h"

#include <stdio.h>

ReplayWriter::ReplayWriter()
{
	bufferBackend = NULL;
	buffer = NULL;

	stepsSinceLastOrder = 0;
}

ReplayWriter::~ReplayWriter()
{
	finish();

	delete buffer;
}

void ReplayWriter::init(const std::string &backend, GameGUI &gui)
{
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

void ReplayWriter::pushOrder(boost::shared_ptr<Order> order)
{
	if (!isValid()) return;
	if (order->getOrderType() == ORDER_VOICE_DATA || order->getOrderType() == ORDER_NULL) return;

	NetSendOrder msg(order);

	// Write the number of steps since last order to this order (can be 0)
	buffer->writeUint16(stepsSinceLastOrder, "replayStepsSinceLastOrder");

	// Write the data of the Order to the file
	msg.encodeData(buffer);

	stepsSinceLastOrder = 0;
}

void ReplayWriter::finish()
{
	if (!isValid()) return;

	// We write a NullOrder to mark the end of the replay (like terminating a string with \0)
	NetSendOrder msg(boost::shared_ptr<Order>(new NullOrder()));

	// Write the number of steps since last order to the end of the replay
	buffer->writeUint16(stepsSinceLastOrder, "replayStepsSinceLastOrder");

	// Write the NullOrder to the file
	msg.encodeData(buffer);

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

	// Write a NullOrder to the file to make sure it's a NullOrder-terminated replay
	NetSendOrder msg(boost::shared_ptr<Order>(new NullOrder()));

	// Write the number of steps since last order to the end of the replay
	file->writeUint16(0, "replayStepsSinceLastOrder");

	// Write the NullOrder to the file
	msg.encodeData(file);

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
