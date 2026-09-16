// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors

#include "NeuroticaReconciler.h"

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
#include <cmath>

static_assert(Neurotica::IntBuildingTypeCount == IntBuildingType::NB_BUILDING,
              "NeuroticaDesiredState.h duplicates NB_BUILDING; the two drifted");
static_assert(Neurotica::SWARM_RATIO_STRIDE == NB_UNIT_TYPE,
              "DesiredState::swarmRatio stride must match the engine's unit-type count");

namespace Neurotica
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
		bindings_.clear();
		boundCells_.clear();
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

	  Delete-and-recreate is the wrong one. It routes through the
	  demolish-persistence gate, so a war flag would take demolishPersistSteps
	  policy steps to follow a battle it should track immediately, and it
	  throws the flag's construction away in the process.

	  So: match desired flag positions against the flags we already have, move
	  what can sensibly be moved, and fall back to create/destroy only for what
	  is left over. This mirrors NewNicowar::compute_defense_flag_positioning
	  (src/ai/nicowar/Flags.cpp), including the two judgements that make it
	  work:

	    * Globally-minimum pairing, not first-come. Each round scans every
	      (flag, vacancy) pair of one type and consumes the closest, rather
	      than walking the map and letting whichever desire is scanned first
	      claim the nearest flag. Raster order is not a preference ordering,
	      and matching by it strands flags far from where they were needed.
	    * Stop, do not stretch. Once the closest surviving pair is beyond
	      maxFlagMoveDist the round ends: everything left is genuinely a
	      create and a destroy, not a move. A flag dragged across the map
	      arrives instantly but its garrison walks.

	  Leftovers need no special handling here. An unmatched vacancy is created
	  by the normal pass, and an unmatched flag falls to the normal demolition
	  path — which keeps the persistence gate, deliberately: Nicowar's target
	  positions are computed and stable, a learned field's will flicker, and
	  the gate is what stops that flicker from costing buildings.
	*/
	void Reconciler::planFlagMoves(const DesiredState &desired, FlagPlan &plan,
	                               std::vector<Candidate> &out)
	{
		if (!map_ || config_.maxFlagMoveDist <= 0)
			return;

		struct Candidate2
		{
			Building *building;
			size_t cell;
			int shortType;
		};
		// Flags the field no longer wants where they currently stand.
		std::vector<Candidate2> orphans;
		for (const auto &entry : observed_)
		{
			Building *b = entry.second;
			if (!b || !b->type || !b->type->isVirtual)
				continue;
			if (desired.building[entry.first] == Uint8(b->type->shortTypeNum + 1))
				continue; // still wanted exactly where it is
			orphans.push_back({b, entry.first, b->type->shortTypeNum});
		}
		if (orphans.empty())
			return;

		// Desired flag positions with nothing standing on them yet.
		struct Vacancy
		{
			size_t cell;
			Sint32 x, y;
			int shortType;
		};
		std::vector<Vacancy> vacancies;
		for (Sint32 y = 0; y < desired.h; y++)
			for (Sint32 x = 0; x < desired.w; x++)
			{
				const size_t i = desired.index(x, y);
				const Uint8 wanted = desired.building[i];
				if (wanted == 0 || observed_.find(i) != observed_.end())
					continue;
				const int shortType = int(wanted) - 1;
				BuildingType *type = placeableType(shortType, nullptr);
				if (!type || !type->isVirtual)
					continue;
				vacancies.push_back({i, x, y, shortType});
			}
		if (vacancies.empty())
			return;

		std::vector<bool> orphanUsed(orphans.size(), false);
		std::vector<bool> vacancyUsed(vacancies.size(), false);

		for (;;)
		{
			int bestDist = config_.maxFlagMoveDist + 1;
			size_t bestOrphan = 0, bestVacancy = 0;
			bool found = false;
			for (size_t o = 0; o < orphans.size(); o++)
			{
				if (orphanUsed[o])
					continue;
				for (size_t v = 0; v < vacancies.size(); v++)
				{
					if (vacancyUsed[v] || vacancies[v].shortType != orphans[o].shortType)
						continue;
					const int dist = map_->warpDistMax(vacancies[v].x, vacancies[v].y,
					                                   orphans[o].building->posX,
					                                   orphans[o].building->posY);
					if (dist < bestDist)
					{
						bestDist = dist;
						bestOrphan = o;
						bestVacancy = v;
						found = true;
					}
				}
			}
			if (!found)
				break; // nothing left within range; the rest are create/destroy

			orphanUsed[bestOrphan] = true;
			vacancyUsed[bestVacancy] = true;
			const Vacancy &target = vacancies[bestVacancy];
			Building *flag = orphans[bestOrphan].building;

			plan.moved[target.cell] = flag;
			plan.vacated.insert(orphans[bestOrphan].cell);
			out.push_back({std::make_shared<OrderMoveFlag>(flag->gid, target.x, target.y, false),
			               desired.urgency[target.cell], sequence_++});
			stats_.flagsMoved++;
		}
	}

	/*
	  Drop bindings that no longer describe anything real, and recompute the set
	  of cells they protect.

	  A binding dies when the desire it served changed or vanished, when the
	  building that satisfied it is gone, or when it was issued long enough ago
	  that the construction site should have appeared and did not. That last
	  case is what stops a single failed order from blocking its desire for the
	  rest of the game.
	*/
	void Reconciler::refreshBindings(const DesiredState &desired, Uint32 tick)
	{
		boundCells_.clear();
		for (auto it = bindings_.begin(); it != bindings_.end();)
		{
			const Binding &binding = it->second;
			const bool stillWanted =
				it->first < desired.building.size() &&
				desired.building[it->first] == Uint8(binding.shortType + 1);
			if (!stillWanted)
			{
				it = bindings_.erase(it);
				continue;
			}

			auto found = observed_.find(binding.actualCell);
			const bool realised = found != observed_.end() && found->second &&
			                      found->second->type &&
			                      found->second->type->shortTypeNum == binding.shortType;
			if (realised)
			{
				boundCells_.insert(binding.actualCell);
				++it;
				continue;
			}

			// Not realised yet. Hold the binding only while the site could
			// still plausibly be on its way.
			if (tick >= binding.issuedTick &&
			    tick - binding.issuedTick < Uint32(config_.pendingBindingTicks))
			{
				boundCells_.insert(binding.actualCell);
				++it;
				continue;
			}
			it = bindings_.erase(it);
		}
	}

	/*
	  Placement: score plus legality, not an exact coordinate.

	  Modelled on AIEcho::Construction::BuildingOrder::find_location, which
	  scores every cell and masks it by passes_constraint, then takes the argmax
	  over what is legal. Echo could survive its preferred tile being occupied
	  because it never named a tile in the first place — it named an intent.

	  Neurotica keeps that split but learns the score half: the field supplies
	  buildingScore, the engine supplies legality, and placement is the best
	  legal cell near the preferred one. Demanding the exact cell instead is
	  what turned a blocked footprint into a desire that could never be
	  satisfied, which is what stalled the oracle at 28 buildings while the
	  teacher went on to 42.

	  Two things Echo did here that are deliberately not copied: it gave up on
	  the order permanently when no location was found, and it signalled that
	  failure as position(0,0) — an ordinary cell on a wrapping map. A desire
	  that cannot be placed this step is simply not placed this step; it is
	  re-diffed on the next one like everything else.
	*/
	bool Reconciler::findNearbyPlacement(const DesiredState &desired, int shortType, Sint32 x,
	                                     Sint32 y, const std::unordered_set<size_t> &claimed,
	                                     Sint32 &outX, Sint32 &outY)
	{
		if (config_.placementSearchRadius <= 0 || !map_)
			return false;

		int bestScore = -1;
		int bestDist = config_.placementSearchRadius + 1;
		bool found = false;

		for (int ring = 1; ring <= config_.placementSearchRadius; ring++)
		{
			for (int dy = -ring; dy <= ring; dy++)
				for (int dx = -ring; dx <= ring; dx++)
				{
					// Only the perimeter of each ring; the interior was covered
					// by a previous, strictly closer ring.
					if (std::max(std::abs(dx), std::abs(dy)) != ring)
						continue;
					const Sint32 cx = map_->normalizeX(x + dx);
					const Sint32 cy = map_->normalizeY(y + dy);
					const size_t cell = desired.index(cx, cy);
					if (claimed.count(cell) || observed_.count(cell))
						continue;
					if (!canPlace(shortType, cx, cy))
						continue;
					const int score = int(desired.buildingScore[cell]);
					if (score > bestScore || (score == bestScore && ring < bestDist))
					{
						bestScore = score;
						bestDist = ring;
						outX = cx;
						outY = cy;
						found = true;
					}
				}
			// Stop at the first ring that yielded anything: a closer legal cell
			// is preferred over a marginally better-scoring distant one, since
			// the field's score was expressed about the preferred cell, not
			// about somewhere several tiles away.
			if (found)
				return true;
		}
		return found;
	}

	void Reconciler::planBuildings(const DesiredState &desired, const FlagPlan &plan,
	                               std::vector<Candidate> &out)
	{
		const Sint32 w = desired.w;
		const Sint32 h = desired.h;
		// Cells taken by a relocation this step, so two displaced desires do
		// not both retarget the same empty tile.
		std::unordered_set<size_t> claimed;

		for (Sint32 y = 0; y < h; y++)
		{
			for (Sint32 x = 0; x < w; x++)
			{
				const size_t i = desired.index(x, y);
				const Uint8 wanted = desired.building[i];
				const Uint8 urgency = desired.urgency[i];

				// A flag's old cell is vacated on purpose, not abandoned by the
				// field, so it must not feed the demolition path.
				if (plan.vacated.count(i))
				{
					emptyStreak_[i] = 0;
					continue;
				}

				auto found = observed_.find(i);
				Building *have = (found == observed_.end()) ? nullptr : found->second;
				// A flag being moved here counts as present: the move order is
				// already queued, so this cell must not also be built on — but
				// its staffing, radius and min-level still need reconciling
				// against the desired field, exactly as for a flag that was
				// already standing here.
				auto arriving = plan.moved.find(i);
				if (arriving != plan.moved.end())
					have = arriving->second;

				// A building standing here to satisfy a desire recorded
				// elsewhere is not an abandoned building.
				if (boundCells_.count(i))
				{
					emptyStreak_[i] = 0;
					continue;
				}

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
					// Already satisfied somewhere else, or on its way there.
					auto bound = bindings_.find(i);
					if (bound != bindings_.end())
					{
						auto at = observed_.find(bound->second.actualCell);
						if (at != observed_.end() && at->second)
						{
							// Reconcile the relocated building's attributes
							// against the desire that asked for it.
							have = at->second;
						}
						else
						{
							continue; // issued, not yet standing
						}
					}
				}

				if (!have)
				{
					Sint32 placeX = x, placeY = y;
					if (!canPlace(shortType, x, y))
					{
						// Preferred cell is not buildable. Relocate rather than
						// drop the desire — see the placement note above.
						if (!findNearbyPlacement(desired, shortType, x, y, claimed, placeX, placeY))
						{
							stats_.illegalSkipped++;
							continue;
						}
						stats_.relocated++;
					}
					const size_t placedCell = desired.index(placeX, placeY);
					claimed.insert(placedCell);
					bindings_[i] = Binding{placedCell, shortType, planTick_};
					boundCells_.insert(placedCell);
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
					out.push_back({std::make_shared<OrderCreate>(team_->teamNumber, placeX, placeY,
					                                             typeNum, unitWorking,
					                                             unitWorkingFuture, radius),
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

	std::vector<std::shared_ptr<Order>> Reconciler::plan(const DesiredState &desired, Uint32 tick)
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

		planTick_ = tick;
		indexObserved();
		refreshBindings(desired, tick);

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
} // namespace Neurotica
