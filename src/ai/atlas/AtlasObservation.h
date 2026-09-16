// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

/*
  Atlas observation planes — the network's input side.

  An observation is one team's fog-limited view of the map, encoded as a stack
  of w*h uint8 planes. It pairs with the desired-state label in AtlasTrace.h to
  form a behaviour-cloning example:

      input  = observation at tick t
      label  = that team's state at tick t + delta   (from the trace)

  The two are recorded into separate files on purpose. The label is a property
  of the FUTURE, so it cannot be written at time t; keeping them apart lets
  delta be swept offline without re-recording anything.

  Fog is honoured: enemy buildings, enemy units and resources are written only
  where the team can see them, and a discovery plane says which cells are
  unknown, remembered or currently visible. AIImplementation.h makes fog
  fairness the implementer's job, and a policy trained on unfogged input would
  simply cheat.

  Static vs dynamic. Terrain and fertility do not change, so they are written
  once in the header rather than in every record — at one record per team per
  ten seconds they would otherwise be most of the file.

  Gradients. Several planes are the engine's own BFS distance fields
  (Map::getResourceGradient and friends). They are the single highest-value
  input here: a convolutional net needs depth proportional to map diameter to
  work out "is that wheat reachable from my base", and these answer it directly
  for free. Stored as 255/(1+d) so near is large and unreachable is zero.

  Format (little-endian, zlib-compressed payloads):

    HEADER
      [4B]  magic "AOB1"
      [4B]  u32 num_records        (patched by close())
      [2B]  u16 map_w
      [2B]  u16 map_h
      [1B]  u8  num_static_planes
      [1B]  u8  num_dynamic_planes
      [1B]  u8  num_teams
      [1B]  u8  sample_period_log2 (informational)
      [4B]  u32 static_compressed_len
      [...]  zlib(static planes, num_static_planes * w * h bytes)

    PER RECORD
      [4B]  u32 tick
      [1B]  u8  team_number
      [1B]  u8  teacher_id
      [2B]  pad
      [4B]  u32 compressed_len
      [...]  zlib(dynamic planes, num_dynamic_planes * w * h bytes)
*/

#include <SDL_stdinc.h>

#include <cstdio>
#include <string>
#include <vector>

class Team;
class Map;

namespace Atlas
{
	//! Planes that never change during a game. Written once.
	enum StaticPlane : Uint8
	{
		SP_TERRAIN_GRASS = 0,
		SP_TERRAIN_SAND,
		SP_TERRAIN_WATER,
		SP_FERTILITY,
		SP_COUNT
	};

	//! Planes written per record. Order is the network's channel order and is
	//! part of the on-disk contract — append only, never reorder.
	enum DynamicPlane : Uint8
	{
		//! Resource amount, fog-limited. One per engine resource type.
		DP_RESOURCE_0 = 0,
		DP_RESOURCE_LAST = DP_RESOURCE_0 + 7,

		//! This team's buildings, one plane per type, filled over the whole
		//! footprint rather than just the anchor so extent is visible.
		DP_MY_BUILDING_0,
		DP_MY_BUILDING_LAST = DP_MY_BUILDING_0 + 12,
		DP_MY_BUILDING_LEVEL,
		DP_MY_BUILDING_SITE,
		DP_MY_BUILDING_WORKERS,

		//! Enemy buildings, fog-limited, one plane per type.
		DP_ENEMY_BUILDING_0,
		DP_ENEMY_BUILDING_LAST = DP_ENEMY_BUILDING_0 + 12,
		DP_ALLY_BUILDING,

		DP_MY_UNIT_WORKER,
		DP_MY_UNIT_EXPLORER,
		DP_MY_UNIT_WARRIOR,
		DP_ENEMY_UNIT_WORKER,
		DP_ENEMY_UNIT_EXPLORER,
		DP_ENEMY_UNIT_WARRIOR,

		DP_AREA_GUARD,
		DP_AREA_CLEAR,
		DP_AREA_FORBIDDEN,

		//! 0 unknown, 128 seen before, 255 visible now.
		DP_DISCOVERY,

		//! Engine BFS distance fields, as 255/(1+d).
		DP_GRAD_WOOD,
		DP_GRAD_WHEAT,
		DP_GRAD_STONE,
		DP_GRAD_FORBIDDEN,
		DP_GRAD_GUARD,
		DP_GRAD_CLEAR,

		DP_COUNT
	};

	//! Fill `out` with SP_COUNT * w * h bytes of static planes.
	bool encodeStaticPlanes(const Map *map, std::vector<Uint8> &out);

	//! Fill `out` with DP_COUNT * w * h bytes for `team`'s current view.
	bool encodeDynamicPlanes(Team *team, std::vector<Uint8> &out);

	/*!
	  Streams observations to disk. Triggered by GLOB2_ATLAS_OBS_PATH, and
	  sampled independently of the trace because observations are two orders of
	  magnitude larger per record.
	*/
	class ObservationWriter
	{
	public:
		~ObservationWriter();

		bool open(const std::string &path, const Map *map, Uint8 numTeams, Uint32 samplePeriod);
		bool isValid() const { return file_ != nullptr; }
		Uint32 samplePeriod() const { return samplePeriod_; }

		void writeRecord(Team *team, Uint32 tick, Uint8 teacherId);
		void close();

	private:
		std::FILE *file_ = nullptr;
		Uint32 records_ = 0;
		Sint32 mapW_ = 0;
		Sint32 mapH_ = 0;
		Uint32 samplePeriod_ = 250;
		std::vector<Uint8> scratch_;
		std::vector<Uint8> compressed_;
	};
} // namespace Atlas
