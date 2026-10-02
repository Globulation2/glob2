// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2010 Michiel De Muynck

#include "ReplayReader.h"

#include "BinaryStream.h"
#include "Order.h"
#include "OrderMessages.h"
#include "GameGUI.h"
#include "Version.h"
#include "Toolkit.h"
#include "FileManager.h"

#include <iomanip>
#include <limits>

ReplayReader::ReplayReader()
{
	stream = NULL;
	currentStep = 0;
	numSteps = 0;
	ordersProcessed = 0;
	numOrders = 0;
	stepsUntilNextOrder = -1;
	versionMinor = VERSION_MINOR;
	checksum = 0;
}

ReplayReader::~ReplayReader()
{
	delete stream;
	stream = NULL;
}

bool ReplayReader::loadReplay(GAGCore::InputStream *inputStream, bool skipToOrders)
{
	// Reset checksum
	checksum = 0;

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

	try
	{
		GAGCore::BinaryInputStream::CheckedReads checked(stream);
		currentStep = 0;
		ordersProcessed = 0;
		numSteps = 0;
		numOrders = 0;
		if (skipToOrders)
		{
			GameGUI tempGui;
			if (!tempGui.load(stream)) throw std::ios_base::failure("Invalid replay game header");
		}
		const Uint16 major = stream->readUint16("versionMajor");
		const Uint16 minor = stream->readUint16("versionMinor");
		if (major != VERSION_MAJOR || minor < REPLAY_MINIMUM_VERSION_MINOR || minor > VERSION_MINOR)
			throw std::ios_base::failure("Unsupported replay version");
		versionMinor = minor;
		if (!stream->canSeek() || stream->getPosition() > static_cast<size_t>(std::numeric_limits<int>::max()))
			throw std::ios_base::failure("Invalid replay position");
		const int pos = static_cast<int>(stream->getPosition());
		for (;;)
		{
			const Uint32 steps = stream->readUint32("replayStepCounter");
			NetSendOrder msg;
			msg.setDecodeVersionMinor(versionMinor);
			msg.decodeData(stream);
			if (steps > std::numeric_limits<Uint32>::max() - numSteps ||
				numOrders == std::numeric_limits<Uint32>::max())
				throw std::ios_base::failure("Replay counters overflow");
			numSteps += steps;
			++numOrders;
			if (msg.getOrder()->getOrderType() == ORDER_NULL) break;
		}
		stream->seekFromStart(pos);
		stepsUntilNextOrder = stream->readUint32("replayStepCounter");
		return true;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Error reading replay: " << error.what() << std::endl;
		delete stream;
		stream = NULL;
		return false;
	}
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

void ReplayReader::setCheckSum(Uint32 checksum)
{
	this->checksum = checksum;
}

std::shared_ptr<Order> ReplayReader::retrieveOrder()
{
	if (!hasMoreOrdersThisStep()) return std::shared_ptr<Order>(new NullOrder());
	assert(isValid());

	std::shared_ptr<Order> order;

	try
	{
		GAGCore::BinaryInputStream::CheckedReads checked(stream);
		// Read the order from the stream
		NetSendOrder msg;
		msg.setDecodeVersionMinor(versionMinor);
		msg.decodeData(stream);
		order = msg.getOrder();

		// Check the checksums (no assert as we also want to check this in release-mode)
		if (checksum != 0 && order->gameCheckSum != 0 && checksum != order->gameCheckSum)
		{
			// Mayday, checksums don't match!
			std::cerr << "Error in replay: checksums don't match!" << std::endl;
			std::cerr << std::setbase(16);
			std::cerr << "\tCurrent game's checksum: 0x" << checksum << std::endl;
			std::cerr << "\tChecksum in replay file: 0x" << order->gameCheckSum << std::endl;
			std::cerr << std::setbase(10);

			throw std::ios_base::failure("Replay checksum mismatch");
		}
		if (order->getOrderType() != ORDER_NULL)
			stepsUntilNextOrder = stream->readUint32("replayStepCounter");
	}
	catch (const std::exception &e)
	{
		// The backing file may have changed since the initial scan.
		std::cerr << "Error reading replay: " << e.what() << std::endl;
		delete stream;
		stream = NULL;
		return std::shared_ptr<Order>(new NullOrder());
	}

	ordersProcessed++;

	return order;
}
