// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2010 Michiel De Muynck

#pragma once

#include <memory>
#include "ReplayTelemetry.h"
#include "ChecksumSample.h"
#include <map>
#include <deque>
#include <assert.h>
#include <string>
#include "Types.h"

namespace GAGCore
{
	class InputStream;
}

class Order;

//! Malformed or truncated replay bodies are rejected, including partial recordings.

//! Oldest replay format (the VERSION_MINOR the replay was written with) that
//! the reader still accepts. Version 169 delays cold ground gradient publication;
//! version 168 changes checksum coverage and order metadata.
//! Save compatibility has a separate floor in Version.h.
static constexpr Uint16 REPLAY_MINIMUM_VERSION_MINOR = 169;

/// This class is used for reading replays.
/// The replay stream is kept open and read every time you do retrieveOrder.
/// Source-tagged checksums are validated when their local snapshot results arrive.
/// Call finishChecksumValidation() after draining terminal results to reject missing
/// checks. Synchronous harnesses can instead supply hashes with setCheckSum().
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
    Uint32 formatVersion() const { return versionMinor; }

	/// Returns true if the replay has finished
	bool isFinished() const;

	/// Increments the current step number
	void advanceStep();

	/// Synchronous harness API. The engine uses source-tagged publishChecksum instead.
    /// Set the checksum. 0 means no testing is done for checksum matches.
	void setCheckSum(Uint32 checksum = 0);
    void enableDeferredChecksums() { deferredChecksums=true; }
    void publishChecksum(ChecksumSample sample);
    void finishChecksumValidation();

	/// Get the next order on the current step
	std::shared_ptr<Order> retrieveOrder();

	void applyTelemetry(Game &game) { telemetry.apply(game, currentStep); }
	bool hasTelemetry() const { return telemetry.available(); }

  private:
    bool deferredChecksums = false;
    std::map<Uint32,Uint32> expectedChecksums, readyChecksums;
    std::deque<Uint32> readyChecksumOrder;
    void expectChecksum(ChecksumSample sample);
    void validateChecksum(Uint32 expected, Uint32 actual);
    ReplayTelemetry::Stream telemetry;
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
	Uint32 stepsUntilNextOrder;

	/// Format version from the replay header, used to decode orders.
	Uint32 versionMinor;

	/// The game's current checksum (or 0 if it's not given)
	Uint32 checksum;
};
