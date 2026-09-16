// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "AINeurotica.h"

#include "NeuroticaFieldSource.h"
#include "Game.h"
#include "Order.h"
#include "Player.h"
#include "Stream.h"
#include "Team.h"

#include <cstdlib>
#include <iostream>

AINeurotica::AINeurotica(Player *player)
{
	init(player);
}

AINeurotica::~AINeurotica() {}

void AINeurotica::init(Player *player)
{
	player_ = player;
	team_ = player_ ? player_->team : nullptr;
	game_ = team_ ? team_->game : nullptr;

	Neurotica::ReconcilerConfig config;
	reconciler_.init(team_, config);

	if (team_)
	{
		// GLOB2_NEUROTICA_ORACLE points at a trace recorded from a teacher AI;
		// GLOB2_NEUROTICA_ORACLE_DELTA is the label horizon in ticks. Together
		// they run the M0 gate: drive the reconciler from a strong AI's own
		// future map and see whether the declarative representation is enough
		// to reproduce its play.
		const char *oraclePath = getenv("GLOB2_NEUROTICA_ORACLE");
		if (oraclePath)
		{
			auto trace = std::make_shared<Neurotica::TraceReader>();
			if (trace->load(oraclePath))
			{
				Uint32 delta = 500;
				if (const char *env = getenv("GLOB2_NEUROTICA_ORACLE_DELTA"))
				{
					const long parsed = strtol(env, nullptr, 10);
					if (parsed > 0)
						delta = Uint32(parsed);
				}
				source_ = std::make_unique<Neurotica::OracleFieldSource>(team_, trace, delta);
			}
			else
			{
				std::cerr << "GLOB2_NEUROTICA_ORACLE: failed to load trace " << oraclePath
				          << "; falling back to the identity field" << std::endl;
			}
		}

		// Default source is the identity field: Neurotica asks for exactly what it
		// already has, so it sits inert and emits nothing. That is the right
		// default for an AI whose policy has not been attached yet — a
		// half-wired Neurotica should do nothing, not something arbitrary.
		if (!source_)
			source_ = std::make_unique<Neurotica::IdentityFieldSource>(team_);
	}

	queue_.clear();
	lastPlanTick_ = -1;
	lastPlanSize_ = 0;
}

void AINeurotica::setFieldSource(std::unique_ptr<Neurotica::FieldSource> source)
{
	source_ = std::move(source);
	queue_.clear();
	lastPlanTick_ = -1;
}

bool AINeurotica::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	(void)stream;
	(void)versionMinor;
	// Nothing Neurotica-shaped is persisted (see AINeurotica.h): the field is
	// recomputed from the live map on the next policy step, and the
	// anti-thrash counters are safe to lose. Re-initialising against the
	// loaded player is the whole of load().
	init(player);
	return true;
}

void AINeurotica::save(GAGCore::OutputStream *stream)
{
	(void)stream;
}

std::shared_ptr<Order> AINeurotica::getOrder(void)
{
	if (!team_ || !game_ || !source_)
		return std::make_shared<NullOrder>();

	const Sint64 tick = Sint64(game_->stepCounter);
	const int period = reconciler_.config().policyPeriodTicks;

	if (lastPlanTick_ < 0 || tick - lastPlanTick_ >= Sint64(period))
	{
		lastPlanTick_ = tick;
		// Drop anything still queued from the previous plan. The new field is
		// the current intent; draining stale orders would act on a target the
		// policy has already moved on from, which is exactly the incoherence
		// the level-triggered design exists to avoid.
		queue_.clear();
		const bool gotField = source_->field(Uint32(tick), desired_);
		if (gotField)
		{
			auto orders = reconciler_.plan(desired_, Uint32(tick));
			lastPlanSize_ = orders.size();
			for (auto &order : orders)
				queue_.push_back(std::move(order));
		}
		else
		{
			lastPlanSize_ = 0;
		}

		// GLOB2_NEUROTICA_DEBUG=<n> prints plan statistics every n policy steps.
		// Distinguishing "planned and found nothing to do" from "never planned"
		// is not possible from the order stream alone, and the difference is
		// the whole diagnosis when Neurotica goes quiet.
		static const int debugEvery = []() {
			const char *env = getenv("GLOB2_NEUROTICA_DEBUG");
			return env ? int(strtol(env, nullptr, 10)) : 0;
		}();
		if (debugEvery > 0 && (lastPlanTick_ / period) % debugEvery == 0)
		{
			const auto &s = reconciler_.stats();
			int wanted = 0;
			for (Uint8 cell : desired_.building)
				if (cell)
					wanted++;
			std::cerr << "NEUROTICA tick=" << tick << " team=" << int(team_->teamNumber)
			          << " src=" << source_->name() << " field=" << (gotField ? "ok" : "none")
			          << " wanted=" << wanted << " orders=" << lastPlanSize_
			          << " created=" << s.created << " demolished=" << s.demolished
			          << " upgraded=" << s.upgraded << " restaffed=" << s.restaffed
			          << " swarms=" << s.swarmsRetuned << " flagsMoved=" << s.flagsMoved
			          << " areas=" << s.areaOrders << " illegal=" << s.illegalSkipped << " (fog=" << s.illegalFog
			          << " occupied=" << s.illegalOccupied << ")"
			          << " capped=" << s.cappedOut << std::endl;
		}
	}

	if (queue_.empty())
		return std::make_shared<NullOrder>();

	auto order = queue_.front();
	queue_.pop_front();
	return order;
}
