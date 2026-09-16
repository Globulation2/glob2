// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

/*
  Where Atlas's desired-state field comes from.

  AIAtlas itself has no opinion about the map; it polls a FieldSource every
  policy step and hands whatever comes back to the reconciler. That indirection
  is what lets the same actuator be driven by an oracle (M0), a neural net over
  a socket (M2) and an in-process policy (M4) without the AI changing.
*/

#include "AtlasDesiredState.h"
#include "AtlasTrace.h"

#include <SDL_stdinc.h>

#include <memory>

class Team;
class Map;

namespace Atlas
{
	class FieldSource
	{
	public:
		virtual ~FieldSource() {}

		//! Produce the field wanted at `tick`. Returning false leaves Atlas
		//! inert for this policy step — the previous field is NOT reused,
		//! because a stale target is worse than no target: the reconciler
		//! would keep driving toward a plan the policy has abandoned.
		virtual bool field(Uint32 tick, DesiredState &out) = 0;

		//! Short name for telemetry and logs.
		virtual const char *name() const = 0;
	};

	/*!
	  Emits the team's CURRENT state as its desired state.

	  This is the reconciler's identity element, and it earns its place twice
	  over:

	    * As a test. Desired == observed must produce exactly zero orders. If
	      it doesn't, the diff, the top-left anchoring convention, or the area
	      comparison is wrong — and this catches that without a trained net, a
	      trace file or a GPU.
	    * As the M1 label extractor. A behaviour-cloning label is precisely
	      "the state this team had at tick t + delta", so the same code that
	      makes the identity field makes the training target.
	*/
	class IdentityFieldSource : public FieldSource
	{
	public:
		explicit IdentityFieldSource(Team *team) : team_(team) {}

		bool field(Uint32 tick, DesiredState &out) override;
		const char *name() const override { return "identity"; }

		//! Snapshot `team`'s live state into `out`. Static entry point so the
		//! dataset writer can build labels for a team it does not own.
		static bool snapshot(Team *team, DesiredState &out);

	private:
		Team *team_ = nullptr;
	};

	/*!
	  Replays a recorded trace as the desired field, offset forward in time.

	  This is the M0 gate. It answers the question the whole declarative design
	  rests on: is a desired-state field a sufficient action representation for
	  Glob2? Drive the reconciler with a strong AI's own future map and, if the
	  representation is adequate, Atlas should play roughly as that AI did —
	  with no policy, no network and no training involved.

	  The field at tick t is the teacher's state at tick t + delta. Small delta
	  means "build what the teacher had almost immediately", which is easy to
	  satisfy and tests little; large delta means "build what the teacher would
	  eventually have", which is a stronger plan but further from anything
	  currently reachable. Sweeping delta here is how the M2 label horizon gets
	  chosen, before it costs a training run.

	  Divergence is expected and is not a bug: once Atlas plays differently
	  from the recording, the trace's later states stop describing a map Atlas
	  actually has. The test is therefore informative over the opening
	  thousands of ticks rather than a whole game.
	*/
	class OracleFieldSource : public FieldSource
	{
	public:
		OracleFieldSource(Team *team, std::shared_ptr<TraceReader> trace, Uint32 delta);

		bool field(Uint32 tick, DesiredState &out) override;
		const char *name() const override { return "oracle"; }

	private:
		Team *team_ = nullptr;
		std::shared_ptr<TraceReader> trace_;
		Uint32 delta_ = 0;
	};
} // namespace Atlas
