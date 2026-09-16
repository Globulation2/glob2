// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

/*
  Atlas desired-state traces.

  A trace is a recording of what a team's map actually looked like, sampled at
  the Atlas policy cadence. It exists because of the labelling identity at the
  heart of this project:

      the desired state at tick t  :=  the observed state at tick t + delta

  So a trace of a strong AI playing is simultaneously (a) the oracle that drives
  the M0 reconciler test and (b) the behaviour-cloning label stream for M2. Both
  read the same file.

  Teacher-agnostic by construction: a snapshot records what a team HAD, never
  how its AI decided, so any AI can be recorded with no per-AI integration.
  Traces are pooled across teachers without conditioning on teacher identity
  (that is a deliberate design choice — see the project notes); `teacherId` and
  `outcome` are nonetheless recorded per snapshot because they cost one byte
  each and keep a later A/B possible without regenerating the corpus.

  Compactness matters: a full DesiredState is w*h*8 bytes (128 KB on a 128x128
  map), and at one snapshot per second a 90k-tick game would be ~460 MB. So the
  wire form stores a building LIST plus run-length-encoded area layers instead
  of dense planes, which brings a typical game to a few MB. TraceReader expands
  back to a dense DesiredState on demand.

  Format (little-endian):

    HEADER (16 bytes)
      [4B] magic "ATR1"
      [4B] u32 num_snapshots      (patched by close())
      [2B] u16 map_w
      [2B] u16 map_h
      [1B] u8  num_teams
      [1B] u8  policy_period      (ticks between snapshots)
      [2B] pad

    PER SNAPSHOT
      [4B] u32 tick
      [1B] u8  team_number
      [1B] u8  teacher_id         (AI::ImplementationID of the team's AI)
      [2B] u16 num_buildings
      per building (8 bytes):
        [2B] u16 x                (top-left anchor, matching OrderCreate)
        [2B] u16 y
        [1B] u8  short_type       (IntBuildingType::Number)
        [1B] u8  level            (BuildingType::level, 0-based)
        [1B] u8  workers          (Building::maxUnitWorking)
        [1B] u8  flag_radius      (unitStayRange; 0 for non-virtual)
      [4B] u32 area_rle_pairs
      per pair (3 bytes):
        [1B] u8  area bits        (Atlas::AreaBit mask)
        [2B] u16 run length

    FOOTER
      [4B] magic "ATRE"
      [1B] u8  num_teams
      [num_teams] u8 outcome      (0 unknown, 1 won, 2 lost)

  The footer carries outcomes because they are not known until the game ends.
  Readers locate it by consuming exactly num_snapshots snapshots first.
*/

#include "AtlasDesiredState.h"

#include <cstdio>
#include <string>
#include <vector>

class Team;
class Game;

namespace Atlas
{
	enum TraceOutcome : Uint8
	{
		OUTCOME_UNKNOWN = 0,
		OUTCOME_WON = 1,
		OUTCOME_LOST = 2
	};

	struct TraceBuilding
	{
		Uint16 x = 0;
		Uint16 y = 0;
		Uint8 shortType = 0;
		Uint8 level = 0;
		Uint8 workers = 0;
		Uint8 flagRadius = 0;
	};

	struct TraceSnapshot
	{
		Uint32 tick = 0;
		Uint8 teamNumber = 0;
		Uint8 teacherId = 0;
		std::vector<TraceBuilding> buildings;
		//! Dense w*h area bitmasks, expanded from the stored RLE.
		std::vector<Uint8> areas;
	};

	/*!
	  Streams snapshots to disk during a game. Triggered by
	  GLOB2_ATLAS_TRACE_PATH, mirroring GLOB2_DATASET_PATH.
	*/
	class TraceWriter
	{
	public:
		~TraceWriter();

		bool open(const std::string &path, Sint32 mapW, Sint32 mapH, Uint8 numTeams,
		          Uint8 policyPeriod);
		bool isValid() const { return file_ != nullptr; }
		Uint8 policyPeriod() const { return policyPeriod_; }

		//! Append one snapshot of `team`'s current state.
		void writeSnapshot(Team *team, Uint32 tick, Uint8 teacherId);

		//! Patch the snapshot count, append the outcome footer, and close.
		//! Safe to call twice; the second call is a no-op.
		void close(const std::vector<Uint8> &outcomes);

	private:
		std::FILE *file_ = nullptr;
		Uint32 snapshots_ = 0;
		Sint32 mapW_ = 0;
		Sint32 mapH_ = 0;
		Uint8 numTeams_ = 0;
		Uint8 policyPeriod_ = 0;
	};

	/*!
	  Loads a whole trace into memory. Traces are a few MB, and both consumers
	  (the oracle source and the label pipeline) do random access by tick, so
	  streaming would buy nothing.
	*/
	class TraceReader
	{
	public:
		bool load(const std::string &path);

		Sint32 mapW() const { return mapW_; }
		Sint32 mapH() const { return mapH_; }
		Uint8 policyPeriod() const { return policyPeriod_; }
		Uint8 outcome(Uint8 teamNumber) const;
		size_t size() const { return snapshots_.size(); }

		//! The earliest snapshot for `teamNumber` at or after `tick`, or
		//! nullptr when the trace ends before then. Returning nullptr rather
		//! than clamping to the last snapshot is deliberate: past the end of
		//! the recording there is no evidence of what the teacher wanted, and
		//! replaying its final state forever would be an invented label.
		const TraceSnapshot *at(Uint8 teamNumber, Uint32 tick) const;

		//! Expand a snapshot into a dense desired-state field.
		bool expand(const TraceSnapshot &snapshot, DesiredState &out) const;

	private:
		std::vector<TraceSnapshot> snapshots_;
		std::vector<Uint8> outcomes_;
		Sint32 mapW_ = 0;
		Sint32 mapH_ = 0;
		Uint8 policyPeriod_ = 0;
	};
} // namespace Atlas
