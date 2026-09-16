// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "AtlasReconciler.h"

#include "BitArray.h"
#include "Brush.h"
#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "Map.h"
#include "Order.h"
#include "Player.h"
#include "Team.h"
#include "UnitConsts.h"

#include <algorithm>

static_assert(Atlas::SWARM_RATIO_STRIDE == NB_UNIT_TYPE,
              "DesiredState::swarmRatio stride must match the engine's unit-type count");

namespace Atlas
{
	namespace
	{
		//! Resolve the BuildingType a fresh player placement of `shortType`
		//! would create. Returns nullptr for an out-of-range short type so the
		//! caller can treat a malformed field as "nothing wanted" rather than
		//! asserting — a learned field will eventually emit garbage and the
		//! reconciler must survive it.
		BuildingType *placeableType(int shortType, Sint32 *typeNumOut)
		{
			if (shortType < 0 || shortType >= IntBuildingType::NB_BUILDING)
				return nullptr;
			const std::string &name = IntBuildingType::reverseConversionMap[shortType];
			if (name.empty())
				return nullptr;
			const Sint32 typeNum = globalContainer->buildingsTypes.getPlaceableTypeNum(name);
			if (typeNum < 0)
				return nullptr;
			if (typeNumOut)
				*typeNumOut = typeNum;
			return globalContainer->buildingsTypes.get(typeNum);
		}
	} // namespace

	void Reconciler::init(Team *team, const ReconcilerConfig &config)
	{
		config_ = config;
		team_ = team;
		game_ = team_ ? team_->game : nullptr;
		map_ = game_ ? &game_->map : nullptr;
		observed_.clear();
		emptyStreak_.clear();
		sequence_ = 0;
		stats_.clear();
	}

	void Reconciler::indexObserved()
	{
		observed_.clear();
		if (!team_ || !map_)
			return;
		for (int i = 0; i < Building::MAX_COUNT; i++)
		{
			Building *b = team_->myBuildings[i];
			if (!b)
				continue;
			// DEAD buildings are already gone from the player's point of view;
			// everything else — including WAITING_FOR_CONSTRUCTION and
			// WAITING_FOR_CONSTRUCTION_ROOM — counts as observed. That is the
			// in-flight rule from the header: a site under construction
			// satisfies a desire for the finished building, otherwise we
			// re-issue OrderCreate for the whole build time.
			if (b->buildingState == Building::DEAD)
				continue;
			const Sint32 x = map_->normalizeX(b->posX);
			const Sint32 y = map_->normalizeY(b->posY);
			observed_[size_t(y) * size_t(map_->getW()) + size_t(x)] = b;
		}
	}

	bool Reconciler::canPlace(int shortType, int x, int y)
	{
		if (!map_ || !team_)
			return false;
		Sint32 typeNum = -1;
		BuildingType *type = placeableType(shortType, &typeNum);
		if (!type)
			return false;
		// Never place into fog. The engine would accept it, but building
		// blind is not something we want the policy to discover as a trick —
		// and AIImplementation.h makes fog fairness the implementer's job.
		if (!map_->isMapDiscovered(x, y, team_->me))
		{
			stats_.illegalFog++;
			return false;
		}
		// Virtual buildings (war / exploration / clearing flags) do not occupy
		// the ground-occupancy map, so the footprint test does not apply.
		if (type->isVirtual)
			return true;
		if (!map_->isFreeForBuilding(x, y, type->width, type->height))
		{
			stats_.illegalOccupied++;
			return false;
		}
		return true;
	}

	/*
	  Flag identity.

	  A desired-state field describes configurations, not objects. "Flag at A"
	  this step and "flag at B" next step is the same picture whether the
	  player moved one flag or destroyed one and built another — the field
	  cannot tell us which, so the reconciler has to pick a reading.

	  Delete-and-recreate is the wrong one for flags. It routes through the
	  demolish-persistence gate, so a war flag would take demolishPersistSteps
	  policy steps to follow a battle it should track immediately, and it
	  throws away the flag's construction in the process. So: match each unmet
	  flag desire against a flag of the same type that the field no longer
	  wants where it stands, and retarget it with OrderMoveFlag.

	  Greedy nearest-first matching. It is not optimal assignment, but flags of
	  one type are few and the cost of a suboptimal pairing is a slightly
	  longer walk, not a wrong plan.
	*/
	void Reconciler::planFlagMoves(const DesiredState &desired, FlagPlan &plan,
	                               std::vector<Candidate> &out)
	{
		if (!map_ || config_.maxFlagMoveDist <= 0)
			return;

		// Flags the field no longer wants where they currently stand.
		struct Orphan
		{
			Building *building;
			size_t cell;
			int shortType;
			bool taken;
		};
		std::vector<Orphan> orphans;
		for (const auto &entry : observed_)
		{
			Building *b = entry.second;
			if (!b || !b->type || !b->type->isVirtual)
				continue;
			const int shortType = b->type->shortTypeNum;
			if (desired.building[entry.first] == Uint8(shortType + 1))
				continue; // still wanted exactly where it is
			orphans.push_back({b, entry.first, shortType, false});
		}
		if (orphans.empty())
			return;

		for (Sint32 y = 0; y < desired.h; y++)
			for (Sint32 x = 0; x < desired.w; x++)
			{
				const size_t i = desired.index(x, y);
				const Uint8 wanted = desired.building[i];
				if (wanted == 0)
					continue;
				if (observed_.find(i) != observed_.end())
					continue; // something is already here; not a move target
				const int shortType = int(wanted) - 1;
				Sint32 typeNum = -1;
				BuildingType *type = placeableType(shortType, &typeNum);
				if (!type || !type->isVirtual)
					continue;

				Orphan *best = nullptr;
				int bestDist = config_.maxFlagMoveDist + 1;
				for (Orphan &orphan : orphans)
				{
					if (orphan.taken || orphan.shortType != shortType)
						continue;
					const int dist = map_->warpDistMax(
						x, y, orphan.building->posX, orphan.building->posY);
					if (dist < bestDist)
					{
						bestDist = dist;
						best = &orphan;
					}
				}
				if (!best)
					continue;

				best->taken = true;
				plan.satisfied.insert(i);
				plan.vacated.insert(best->cell);
				out.push_back({std::make_shared<OrderMoveFlag>(best->building->gid, x, y, false),
				               desired.urgency[i], sequence_++});
				stats_.flagsMoved++;
			}
	}

	void Reconciler::planBuildings(const DesiredState &desired, const FlagPlan &plan,
	                               std::vector<Candidate> &out)
	{
		const Sint32 w = desired.w;
		const Sint32 h = desired.h;

		for (Sint32 y = 0; y < h; y++)
		{
			for (Sint32 x = 0; x < w; x++)
			{
				const size_t i = desired.index(x, y);
				const Uint8 wanted = desired.building[i];
				const Uint8 urgency = desired.urgency[i];

				// A cell already handled by a flag move needs nothing further:
				// the destination is satisfied, and the source has been
				// vacated on purpose rather than abandoned.
				if (plan.satisfied.count(i) || plan.vacated.count(i))
				{
					emptyStreak_[i] = 0;
					continue;
				}

				auto found = observed_.find(i);
				Building *have = (found == observed_.end()) ? nullptr : found->second;

				if (wanted == 0)
				{
					// Absence of desire is not a demolition order. Only a
					// sustained empty desire, on a cell the policy has not
					// marked committed, earns an OrderDelete.
					if (!have)
					{
						emptyStreak_[i] = 0;
						continue;
					}
					if (desired.commit[i] >= config_.commitBlocksDemolish)
					{
						emptyStreak_[i] = 0;
						continue;
					}
					if (emptyStreak_[i] < 0xFFFF)
						emptyStreak_[i]++;
					if (int(emptyStreak_[i]) >= config_.demolishPersistSteps)
					{
						out.push_back({std::make_shared<OrderDelete>(have->gid), urgency, sequence_++});
						stats_.demolished++;
					}
					continue;
				}

				emptyStreak_[i] = 0;
				const int shortType = int(wanted) - 1;

				if (!have)
				{
					if (!canPlace(shortType, x, y))
					{
						stats_.illegalSkipped++;
						continue;
					}
					Sint32 typeNum = -1;
					BuildingType *type = placeableType(shortType, &typeNum);
					if (!type)
						continue;
					const Uint8 staff = desired.workers[i];
					const Uint8 staffFuture = desired.workersFuture[i];
					const Sint32 unitWorking =
						(staff == DONT_CARE) ? type->maxUnitWorking : Sint32(staff);
					const Sint32 unitWorkingFuture =
						(staffFuture == DONT_CARE) ? unitWorking : Sint32(staffFuture);
					std::optional<Sint32> radius;
					if (type->isVirtual && desired.flagRadius[i] != DONT_CARE)
						radius = Sint32(desired.flagRadius[i]);
					out.push_back({std::make_shared<OrderCreate>(team_->teamNumber, x, y, typeNum,
					                                             unitWorking, unitWorkingFuture, radius),
					               urgency, sequence_++});
					stats_.created++;
					continue;
				}

				// A building is already here. Either it is the wrong thing —
				// which is a replacement, and replacements go through the same
				// persistence gate as a plain demolition, since a flickering
				// type plane is exactly as expensive as a flickering presence
				// plane — or it is the right thing and may need adjusting.
				if (!have->type || have->type->shortTypeNum != shortType)
				{
					if (desired.commit[i] >= config_.commitBlocksDemolish)
						continue;
					if (emptyStreak_[i] < 0xFFFF)
						emptyStreak_[i]++;
					if (int(emptyStreak_[i]) >= config_.demolishPersistSteps)
					{
						out.push_back({std::make_shared<OrderDelete>(have->gid), urgency, sequence_++});
						stats_.demolished++;
					}
					continue;
				}

				// Right type in the right place: reconcile its attributes.
				const Uint8 wantLevel = desired.level[i];
				if (wantLevel != DONT_CARE && have->type &&
				    Sint32(wantLevel) > have->type->level &&
				    have->constructionResultState == Building::NO_CONSTRUCTION)
				{
					const Sint32 staff = (desired.workers[i] == DONT_CARE)
					                         ? have->maxUnitWorking
					                         : Sint32(desired.workers[i]);
					out.push_back({std::make_shared<OrderConstruction>(have->gid, staff, staff),
					               urgency, sequence_++});
					stats_.upgraded++;
				}

				const Uint8 wantStaff = desired.workers[i];
				if (wantStaff != DONT_CARE && Sint32(wantStaff) != have->maxUnitWorking)
				{
					out.push_back({std::make_shared<OrderModifyBuilding>(have->gid, Uint16(wantStaff)),
					               urgency, sequence_++});
					stats_.restaffed++;
				}

				const Uint8 wantRadius = desired.flagRadius[i];
				if (wantRadius != DONT_CARE && have->type && have->type->isVirtual &&
				    Sint32(wantRadius) != have->unitStayRange)
				{
					out.push_back({std::make_shared<OrderModifyFlag>(have->gid, Sint32(wantRadius)),
					               urgency, sequence_++});
					stats_.flagsRetuned++;
				}

				// Unit production. Without this a swarm is placed but never
				// told what to make, so the team builds the teacher's base and
				// then fields the default unit mix.
				const size_t ratioBase = i * SWARM_RATIO_STRIDE;
				if (have->type && have->type->shortTypeNum == IntBuildingType::SWARM_BUILDING &&
				    desired.swarmRatio[ratioBase] != DONT_CARE)
				{
					Sint32 wanted[NB_UNIT_TYPE];
					bool differs = false;
					for (int u = 0; u < NB_UNIT_TYPE; u++)
					{
						const Uint8 value = desired.swarmRatio[ratioBase + size_t(u)];
						wanted[u] = (value == DONT_CARE) ? have->ratio[u] : Sint32(value);
						if (wanted[u] != have->ratio[u])
							differs = true;
					}
					if (differs)
					{
						out.push_back({std::make_shared<OrderModifySwarm>(have->gid, wanted),
						               urgency, sequence_++});
						stats_.swarmsRetuned++;
					}
				}

				const Uint8 wantPriority = desired.priority[i];
				if (wantPriority != DONT_CARE)
				{
					const Sint32 asSigned = Sint32(wantPriority) - Sint32(PRIORITY_NORMAL);
					if (asSigned != have->priority)
					{
						out.push_back({std::make_shared<OrderChangePriority>(have->gid, asSigned),
						               urgency, sequence_++});
						stats_.repriorised++;
					}
				}

				const Uint8 wantMinLevel = desired.minLevelToFlag[i];
				if (wantMinLevel != DONT_CARE && have->type && have->type->isVirtual &&
				    Sint32(wantMinLevel) != have->minLevelToFlag)
				{
					out.push_back({std::make_shared<OrderModifyMinLevelToFlag>(
					                   have->gid, Uint16(wantMinLevel)),
					               urgency, sequence_++});
					stats_.flagsRetuned++;
				}
			}
		}
	}

	void Reconciler::planAreas(const DesiredState &desired, std::vector<Candidate> &out)
	{
		if (!map_ || !team_)
			return;
		const Sint32 w = desired.w;
		const Sint32 h = desired.h;
		const Uint32 teamMask = Team::teamNumberToMask(team_->teamNumber);

		// One ADD order and one DEL order per area kind per policy step. The
		// wire format is a bounding box plus a bitmask over it, so the diff
		// literally *is* the order: collect the cells that changed in each
		// direction, take their bounding box, and set the bits.
		// The three area layers differ only in which order subclass carries
		// them; the bounding-box/bitmask diff below is identical for all.
		struct Kind
		{
			Uint8 bit;
		};
		const Kind kinds[3] = {{AREA_GUARD}, {AREA_CLEAR}, {AREA_FORBIDDEN}};

		for (const Kind &kind : kinds)
		{
			for (int mode = 0; mode < 2; mode++)
			{
				const bool adding = (mode == 0);
				Sint32 minX = w, minY = h, maxX = -1, maxY = -1;
				Uint8 peakUrgency = 0;

				auto currently = [&](Sint32 x, Sint32 y) -> bool {
					switch (kind.bit)
					{
						case AREA_GUARD: return map_->isGuardArea(x, y, teamMask);
						case AREA_CLEAR: return map_->isClearArea(x, y, teamMask);
						default: return map_->isForbidden(x, y, teamMask);
					}
				};

				for (Sint32 y = 0; y < h; y++)
					for (Sint32 x = 0; x < w; x++)
					{
						const size_t i = desired.index(x, y);
						const bool want = (desired.areas[i] & kind.bit) != 0;
						const bool has = currently(x, y);
						if (want == has || want != adding)
							continue;
						minX = std::min(minX, x);
						minY = std::min(minY, y);
						maxX = std::max(maxX, x);
						maxY = std::max(maxY, y);
						peakUrgency = std::max(peakUrgency, desired.urgency[i]);
					}

				if (maxX < minX || maxY < minY)
					continue;

				// The engine reads the mask over the half-open box
				// [centerX+minX, centerX+maxX) x [centerY+minY, centerY+maxY),
				// y outer and x inner (Game::executeAlterGuardArea). Anchor
				// the box at the origin and use absolute coordinates so the
				// index arithmetic here matches that loop exactly.
				const Sint32 boxW = maxX - minX + 1;
				const Sint32 boxH = maxY - minY + 1;
				if (boxW > ORDER_AREA_BRUSH_MAX_SIDE || boxH > ORDER_AREA_BRUSH_MAX_SIDE)
					continue;

				// getOrderType() is virtual, so the layer is chosen by which
				// subclass we instantiate — the fields are common to all three.
				std::shared_ptr<OrderAlterArea> order;
				switch (kind.bit)
				{
					case AREA_GUARD: order = std::make_shared<OrderAlterGuardArea>(); break;
					case AREA_CLEAR: order = std::make_shared<OrderAlterClearArea>(); break;
					default: order = std::make_shared<OrderAlterForbidden>(); break;
				}
				order->teamNumber = Uint8(team_->teamNumber);
				order->type = adding ? Uint8(BrushTool::MODE_ADD) : Uint8(BrushTool::MODE_DEL);
				order->centerX = 0;
				order->centerY = 0;
				order->minX = Sint16(minX);
				order->minY = Sint16(minY);
				order->maxX = Sint16(maxX + 1);
				order->maxY = Sint16(maxY + 1);
				order->mask = Utilities::BitArray(size_t(boxW) * size_t(boxH), false);

				for (Sint32 y = minY; y <= maxY; y++)
					for (Sint32 x = minX; x <= maxX; x++)
					{
						const size_t i = desired.index(x, y);
						const bool want = (desired.areas[i] & kind.bit) != 0;
						const bool has = currently(x, y);
						if (want == has || want != adding)
							continue;
						const size_t bit = size_t(y - minY) * size_t(boxW) + size_t(x - minX);
						order->mask.set(bit, true);
					}

				out.push_back({std::move(order), peakUrgency, sequence_++});
				stats_.areaOrders++;
			}
		}
	}

	std::vector<std::shared_ptr<Order>> Reconciler::plan(const DesiredState &desired)
	{
		stats_.clear();
		std::vector<std::shared_ptr<Order>> result;
		if (!team_ || !map_ || !desired.valid())
			return result;
		// A field sized for a different map is a producer bug, not something
		// to paper over by clamping — dropping it keeps the AI inert instead
		// of acting on coordinates that mean nothing.
		if (desired.w != map_->getW() || desired.h != map_->getH())
			return result;

		const size_t cells = size_t(desired.w) * size_t(desired.h);
		if (emptyStreak_.size() != cells)
			emptyStreak_.assign(cells, 0);

		indexObserved();

		std::vector<Candidate> candidates;
		FlagPlan flagPlan;
		planFlagMoves(desired, flagPlan, candidates);
		planBuildings(desired, flagPlan, candidates);
		planAreas(desired, candidates);

		// Urgency descending, then emission order, so a tie is resolved by
		// scan order rather than by hash-table iteration.
		std::stable_sort(candidates.begin(), candidates.end(),
		                 [](const Candidate &a, const Candidate &b) {
			                 if (a.urgency != b.urgency)
				                 return a.urgency > b.urgency;
			                 return a.sequence < b.sequence;
		                 });

		if (candidates.size() > config_.maxQueuedOrders)
		{
			stats_.cappedOut = int(candidates.size() - config_.maxQueuedOrders);
			candidates.resize(config_.maxQueuedOrders);
		}

		result.reserve(candidates.size());
		for (auto &candidate : candidates)
			result.push_back(std::move(candidate.order));
		return result;
	}
} // namespace Atlas
