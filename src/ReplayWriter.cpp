// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2010 Michiel De Muynck

#include "ReplayWriter.h"

#include "BinaryStream.h"
#include "StreamBackend.h"
#include "Order.h"
#include "OrderMessages.h"
#include "GameGUI.h"
#include "Version.h"
#include "Toolkit.h"
#include "FileManager.h"

#include <stdio.h>

// Write an Order to the stream, with the given checksum
inline void writeOrder(GAGCore::OutputStream *stream, std::shared_ptr<Order> order, Uint32 checksum = 0)
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
	ordersWritten = 0;
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

	// Initialise the buffer backend.
	// Absolute paths (leading '/') bypass FileManager — its dirList prepend
	// turns "/tmp/foo.replay" into "~/.glob2//tmp/foo.replay" and fails. The
	// AI-trainer pipeline relies on this path being arbitrary (via
	// GLOB2_REPLAY_PATH), so absolute paths must work as written.
	if (backend == "")
	{
		bufferBackend = new MemoryStreamBackend();
	}
	else if (!backend.empty() && backend[0] == '/')
	{
		FILE* fp = fopen(backend.c_str(), "w+");
		bufferBackend = new FileStreamBackend(fp);
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

void ReplayWriter::pushOrder(std::shared_ptr<Order> order)
{
	if (!isValid()) return;
	if (order->getOrderType() == ORDER_VOICE_DATA || order->getOrderType() == ORDER_NULL) return;

	// Write the number of steps since last order to this order (can be 0)
	buffer->writeUint32(stepsSinceLastOrder, "replayStepsSinceLastOrder");

	// Write the Order to the file
	writeOrder(buffer, order, checksum);

	stepsSinceLastOrder = 0;
	ordersWritten++;

	// Don't flush the buffer. That is done when writing the last Order, in ReplayWriter::finish().
}

void ReplayWriter::finish()
{
	if (!isValid()) return;

	// Write the number of steps since last order to the end of the replay
	buffer->writeUint32(stepsSinceLastOrder, "replayStepsSinceLastOrder");

	// We write a NullOrder to mark the end of the replay (like terminating a string with \0)
	writeOrder(buffer, std::shared_ptr<Order>(new NullOrder()), 0);

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
	file->writeUint32(0, "replayStepsSinceLastOrder");

	// Write a NullOrder to the file to make sure it's a NullOrder-terminated replay
	writeOrder(file, std::shared_ptr<Order>(new NullOrder()), 0);

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
