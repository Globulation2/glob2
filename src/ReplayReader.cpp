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

#include "ReplayReader.h"

#include "Stream.h"
#include "Order.h"
#include "NetMessage.h"
#include "GUIMessageBox.h"
#include "GameGUI.h"
#include "Version.h"
#include "Toolkit.h"
#include "FileManager.h"

ReplayReader::ReplayReader()
{
	stream = NULL;
	currentStep = 0;
	numSteps = 0;
	ordersProcessed = 0;
	numOrders = 0;
	stepsUntilNextOrder = -1;
}

ReplayReader::~ReplayReader()
{
	delete stream;
	stream = NULL;
}

bool ReplayReader::loadReplay(GAGCore::InputStream *inputStream, bool skipToOrders)
{
	// Make sure the given stream is valid
	if (inputStream == NULL) return false;
	if (!inputStream->isValid())
	{
		delete inputStream;
		return false;
	}

	// If we still own a different stream, delete that one first
	if (stream != NULL)
	{
		delete stream;
	}

	// From now on we own the given stream
	stream = inputStream;

	currentStep = 0;
	ordersProcessed = 0;
	
	// Skip to the section in the stream where the header ends
	if (skipToOrders)
	{
		try
		{
			// readEnterSection doesn't do anything, so just read the gui that is there and discard the data
			GameGUI tempGui;
			tempGui.load(stream);
		}
		catch (std::exception &e)
		{
			delete stream;
			stream = NULL;
			return false;
		}
	}

	// Read the version numbers
	Uint16 version_major = stream->readUint16("versionMajor");
	Uint16 version_minor = stream->readUint16("versionMinor");

	// Check the version number. Playing a replay of an older version is impossible.
	if (version_major != VERSION_MAJOR || version_minor != VERSION_MINOR)
	{
		delete stream;
		stream = NULL;
		return false;
	}

	// If there are no orders, this is also not a valid replay (there should be at least a NullOrder)
	if (stream->isEndOfStream())
	{
		delete stream;
		stream = NULL;
		return false;
	}

	// Save the position in the stream
	assert(stream->canSeek());
	size_t pos = stream->getPosition();

	// Calculate the length of this replay
	boost::shared_ptr<Order> order;
	numSteps = 0;
	numOrders = 0;
	stepsUntilNextOrder = stream->readUint16("replayStepCounter");
	do
	{
		try
		{
			// Read an order from the stream
			NetSendOrder msg;
			msg.decodeData(stream);
			order = msg.getOrder();

			// If we got here, it means the order was valid, so increase the replay length
			numOrders++;
			numSteps += stepsUntilNextOrder;
		}
		catch (const std::ios_base::failure &e)
		{
			// The order in the stream was invalid
			std::cout << "Error reading replay: " << e.what() << std::endl;

			// If it was a replay with at least a few orders that were correct so far, use to plan B: play the replay up to this order
			if (numOrders < 5)
			{
				// Fail
				delete stream;
				stream = NULL;
				return false;
			}
			else
			{
				// Overwrite the order as if it were a NullOrder
				order = boost::shared_ptr<Order>(new NullOrder());
			}
		}

		// If it was a real order, read and increase numSteps accordingly
		if (order->getOrderType() != ORDER_NULL)
		{
			stepsUntilNextOrder = stream->readUint16("replayStepCounter");
		}
	}
	while (order->getOrderType() != ORDER_NULL);

	// Go back to the original position in the stream
	stream->seekFromStart(pos);
	
	// Read the number of steps until the first order
	stepsUntilNextOrder = stream->readUint16("replayStepCounter");

	// If we get to this point, the replay file should be valid
	assert(isValid());
	return true;
}

bool ReplayReader::loadReplay(const std::string &filename)
{
	InputStream *inputStream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(filename));
	return loadReplay(inputStream, true);
}

bool ReplayReader::isValid() const
{
	return !(stream == NULL || !stream->isValid() || currentStep > numSteps || ordersProcessed > numOrders);
}

bool ReplayReader::hasMoreOrdersThisStep() const
{
	return (stream != NULL && stepsUntilNextOrder == 0 && currentStep <= numSteps && ordersProcessed < numOrders);
}

Uint32 ReplayReader::getCurrentStep() const
{
	if (!isValid()) return 0;
	return currentStep;
}

Uint32 ReplayReader::getNumStepsTotal() const
{
	if (!isValid()) return 1;
	return numSteps;
}

bool ReplayReader::isFinished() const
{
	return (!isValid() || ordersProcessed >= numOrders);
}

void ReplayReader::advanceStep()
{
	currentStep++;
	stepsUntilNextOrder--;
}

boost::shared_ptr<Order> ReplayReader::retrieveOrder()
{
	if (!hasMoreOrdersThisStep()) return boost::shared_ptr<Order>(new NullOrder());
	assert(isValid());

	boost::shared_ptr<Order> order;

	try
	{
		// Read the order from the stream
		NetSendOrder msg;
		msg.decodeData(stream);
		order = msg.getOrder();
	}
	catch (const std::ios_base::failure &e)
	{
		// We shouldn't ever get here. In init() we made sure that all the orders up to numOrders are valid.
		std::cerr << "Error reading replay: " << e.what() << std::endl;
		delete stream;
		stream = NULL;
		assert(false);
	}

	ordersProcessed++;

	// Read the number of steps until the next order, if there is one
	if (order->getOrderType() != ORDER_NULL) stepsUntilNextOrder = stream->readUint16("replayStepCounter");
	else assert(ordersProcessed >= numOrders && currentStep >= numSteps);

	return order;
}

GAGCore::InputStream* ReplayReader::getStream() const
{
	return stream;
}
