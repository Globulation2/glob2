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
#include <algorithm>
#include <array>
#include <ios>
#include <iostream>
#include <filesystem>

// Write an Order to the stream, with the given checksum
inline void writeOrder(GAGCore::OutputStream *stream, std::shared_ptr<Order> order, Uint32 checksum = 0, bool present = false)
{
	// Write the checksum
	order->gameCheckSum = checksum;
    order->hasGameCheckSum = present || checksum!=0;

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
    try { finish(); } catch (const std::exception& e) { std::cerr<<"Cannot finalize replay: "<<e.what()<<std::endl; }

	delete buffer;
}

void ReplayWriter::init(const std::string &backend, GameGUI &gui)
{
	recordingPath = backend;
	// Avoid trouble
	checksum = 0;
	game = gui.replayTelemetryGame();
	telemetry = {};
	telemetryStep = 0;
	finished = false;
	if (game)
		telemetry.capture(*game, 0);

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
		FILE* fp = fopen(backend.c_str(), "w+b");
		bufferBackend = new FileStreamBackend(fp);
	}
	else
	{
		FILE* fp = Toolkit::getFileManager()->openFP(backend, "w+b");
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
	if (finished)
		return;
	++telemetryStep;
	stepsSinceLastOrder++;
}

void ReplayWriter::captureTelemetry()
{
	if (game && !finished)
		telemetry.capture(*game, telemetryStep);
}

void ReplayWriter::setCheckSum(Uint32 checksum)
{
	this->checksum = checksum;
}

void ReplayWriter::pushOrder(std::shared_ptr<Order> order)
{
	if (!isValid() || finished)
		return;
	if (order->getOrderType() == ORDER_VOICE_DATA || order->getOrderType() == ORDER_NULL) return;

    if (flushChecksums) {
        // Orders can be reused/mutated by networking after this call. Freeze payload and sender.
        std::vector<Uint8> bytes(order->getDataLength()+1);
        bytes[0]=order->getOrderType();
        std::copy_n(order->getData(),order->getDataLength(),bytes.data()+1);
        auto copy=Order::getOrder(bytes.data(),bytes.size(),VERSION_MINOR);
        if (!copy) throw std::ios_base::failure("Cannot freeze replay order");
        copy->sender=order->sender;
        pendingOrders.push_back({stepsSinceLastOrder,game ? game->stepCounter : telemetryStep,std::move(copy)});
        writePending();
    } else {
        buffer->writeUint32(stepsSinceLastOrder,"replayStepsSinceLastOrder");
        writeOrder(buffer,order,checksum);
    }

	stepsSinceLastOrder = 0;
	ordersWritten++;

	// Don't flush the buffer. That is done when writing the last Order, in ReplayWriter::finish().
}

void ReplayWriter::publishChecksum(ChecksumSample sample)
{
    if (finished) return;
    auto [entry,inserted]=readyChecksums.emplace(sample.sourceTick,sample.value);
    // Several paused network turns can edit one simulation tick. Playback
    // executes their orders together, so all use that tick's first observation.
    if (inserted) readyChecksumOrder.push_back(sample.sourceTick);
    writePending();
    while (readyChecksumOrder.size()>8) {
        readyChecksums.erase(readyChecksumOrder.front()); readyChecksumOrder.pop_front();
    }
}
void ReplayWriter::writePending()
{
    while (!pendingOrders.empty()) {
        auto& pending=pendingOrders.front();
        const auto found=readyChecksums.find(pending.worldTick);
        if (found==readyChecksums.end()) break;
        buffer->writeUint32(pending.steps,"replayStepsSinceLastOrder");
        pending.order->gameCheckSumTick=pending.worldTick;
        pending.order->hasGameCheckSum=true;
        writeOrder(buffer,pending.order,found->second,true);
        pendingOrders.pop_front();
    }
}

void ReplayWriter::finish()
{
	if (!isValid() || finished)
		return;

    if (flushChecksums) flushChecksums();
    writePending();
    if (!pendingOrders.empty()) throw std::ios_base::failure("Replay has uncomputed checksums");
    writeEnd(buffer);

	finished = true;

	// Flush the buffer now
	buffer->flush();
	if (!recordingPath.empty()) notifySaved(recordingPath);

	stepsSinceLastOrder = 0;
}

void ReplayWriter::writeEnd(GAGCore::OutputStream* output) const
{
    // Write the number of steps since last order to the end of the replay
	output->writeUint32(stepsSinceLastOrder, "replayStepsSinceLastOrder");

	// We write a NullOrder to mark the end of the replay (like terminating a string with \0)
	auto end=std::make_shared<NullOrder>();
    end->gameCheckSumTick=game ? game->stepCounter : 0;
    const auto final=readyChecksums.find(end->gameCheckSumTick);
    end->hasGameCheckSum=final!=readyChecksums.end();
    writeOrder(output,end,final!=readyChecksums.end() ? final->second : 0,final!=readyChecksums.end());

	telemetry.write(output);
}

bool ReplayWriter::write(const std::string &filename) const
{
	if (!isValid()) return false;
	if (filename == "") return false;
	
    if (!finished && flushChecksums) flushChecksums();
    if (!pendingOrders.empty()) return false;
    // Make sure the buffer is flushed
    buffer->flush();

    const size_t pos = bufferBackend->getPosition();
    const bool saved = Toolkit::getFileManager()->writeAtomically(filename, [&](GAGCore::OutputStream& file) {
        bufferBackend->seekFromStart(0);
        // EOF becomes true at different times for memory and FILE backends.
        // Copy the known recording length so neither drops its final byte.
        std::array<Uint8, 4096> bytes;
        for (size_t remaining = pos; remaining;) {
            const auto count = std::min(remaining, bytes.size());
            if (!bufferBackend->readExact(bytes.data(), count))
                throw std::ios_base::failure("Incomplete replay recording");
            file.write(bytes.data(), count, "replayBytes");
            remaining -= count;
        }
		if (!finished)
		{
            writeEnd(&file);
		}
    });
    // Atomic writer failures must not leave the live recording's cursor moved.
    bufferBackend->seekFromStart(pos);
    if (saved) notifySaved(filename);
    return saved;
}

GAGCore::OutputStream* ReplayWriter::getBuffer() const
{
	return buffer;
}

void ReplayWriter::notifySaved(const std::string &filename) const
{
    try {
        // A classic recording can overwrite an identical online recording. Remove
        // its old companion even when the replay hash would still match.
        if (auto *files = Toolkit::getFileManager()) {
            auto path = std::filesystem::path(filename + ".appearance.json");
            if (!path.is_absolute()) path = std::filesystem::path(files->getDir(0)) / path;
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
        if (saveObserver) saveObserver(filename);
    }
    catch (...) { /* Cosmetic metadata must never break replay saving or teardown. */ }
}
