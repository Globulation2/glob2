// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#pragma once

/*
  Atlas — a declarative, spatial AI.

  Atlas does not decide which order to send. It decides what it wants the map
  to look like (a DesiredState field, produced by a pluggable FieldSource) and
  lets AtlasReconciler emit whatever orders close the gap. See
  AtlasDesiredState.h for why the policy is shaped that way.

  Order pacing: the engine polls every AI once per tick and accepts at most one
  order per poll (EngineRun.cpp). Atlas re-plans only every
  ReconcilerConfig::policyPeriodTicks ticks and drains the resulting queue one
  order per tick in between. That decouples how often the policy thinks from
  how fast orders can leave, which matters because a full re-plan can produce
  dozens of orders at once while the channel carries one.

  State and saves: the reconciler's only persistent state is its per-cell
  anti-thrash counters, which are deliberately NOT serialized. Losing them on
  load merely delays the next demolition by a few policy steps, and keeping the
  save format free of Atlas-shaped state means adding Atlas cannot disturb
  save/load compatibility (AGENTS.md).
*/

#include "AIImplementation.h"
#include "AtlasDesiredState.h"
#include "AtlasReconciler.h"

#include <deque>
#include <memory>

class Player;
class Team;
class Game;
class Map;

namespace Atlas
{
	class FieldSource;
}

class AIAtlas : public AIImplementation
{
public:
	AIAtlas(Player *player);
	~AIAtlas();

	bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor) override;
	void save(GAGCore::OutputStream *stream) override;

	std::shared_ptr<Order> getOrder(void) override;

	//! Replace the field source. Ownership transfers. Used by the harnesses to
	//! swap an oracle or a socket-backed policy in for the default.
	void setFieldSource(std::unique_ptr<Atlas::FieldSource> source);

	const Atlas::Reconciler &reconciler() const { return reconciler_; }

	//! Most recent plan size, for the round-trip harness and telemetry.
	size_t lastPlanSize() const { return lastPlanSize_; }

private:
	void init(Player *player);

	Player *player_ = nullptr;
	Team *team_ = nullptr;
	Game *game_ = nullptr;

	std::unique_ptr<Atlas::FieldSource> source_;
	Atlas::Reconciler reconciler_;
	Atlas::DesiredState desired_;
	std::deque<std::shared_ptr<Order>> queue_;

	//! Tick of the last re-plan. Negative means "never planned", which forces
	//! a plan on the first poll rather than waiting out a full period.
	Sint64 lastPlanTick_ = -1;
	size_t lastPlanSize_ = 0;
};
