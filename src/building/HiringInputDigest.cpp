// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiringInputDigest.h"
#include "HiringPlanner.h"
#include "UnitCatalog.h"
#include "TerrainRegistry.h"
#include "ResourceRegistry.h"
#include <ChecksumStream.h>

namespace
{
struct Hash
{
	GAGCore::ChecksumStream stream;
	void add(Uint32 value) { stream.addUint32(value); }
	void wide(Uint64 value)
	{
		add(value >> 32);
		add(Uint32(value));
	}
	template <class Values> void values(const Values &values)
	{
		add(std::size(values));
		for (auto v : values)
			add(Uint32(v));
	}
	void ref(UnitRef r)
	{
		add(r.gid);
		add(r.generation);
	}
	void ref(BuildingRef r)
	{
		add(r.gid);
		add(r.generation);
	}
	Uint32 result() const { return stream.value; }
};
} // namespace
Uint32 HiringInputDigests::digest(const SimulationSnapshot::Handle &w, Uint64 routeIdentity)
{
	Hash h;
	h.add(w.tick);
	h.add(w.width);
	h.add(w.height);
	h.wide(routeIdentity);
	h.add(w.catalogs->units->checksum());
	h.add(w.catalogs->resources->checksum());
	h.stream.writeText(w.catalogs->buildingFingerprint, "catalog");
	h.add(component(w.terrain, 0,
					[](const auto &terrain)
					{
						Hash s;
						s.add(terrain.registry->checksum());
						s.add(terrain.movementModifiers);
						s.add(terrain.airConstraints);
						// Rule-table indices are private numbering. Vertices and the registry
						// define the same logical rules after loading or table compaction.
						s.values(*terrain.vertices);
						return s.result();
					}));
	h.add(component(w.resources, 1,
					[](const auto &r)
					{
						Hash s;
						s.add(r.cells.size());
						for (const auto &c : r.cells)
						{
							s.add(c.resource.getUint32());
							s.add(c.fertility);
							s.add(c.mayGrow);
						}
						s.values(r.stockIndices);
						s.add(r.stocks.size());
						for (const auto &stock : r.stocks)
							s.values(stock);
						s.values(r.materialSourceCounts);
						return s.result();
					}));
	h.add(component(w.occupancy, 2,
					[](const auto &o)
					{
						Hash s;
						s.add(o.cells.size());
						for (const auto &c : o.cells)
						{
							s.add(c.building);
							s.add(c.immobileUnit);
						}
						return s.result();
					}));
	h.add(component(w.areas, 3,
					[](const auto &a)
					{
						Hash s;
						s.add(a.cells.size());
						for (const auto &c : a.cells)
						{
							s.add(c.forbidden);
							s.add(c.guard);
							s.add(c.clear);
						}
						return s.result();
					}));
	h.add(component(w.visibility, 4,
					[](const auto &v)
					{
						Hash s;
						s.values(v.visible);
						return s.result();
					}));
	h.add(component(w.teams, 5,
					[](const auto &teams)
					{
						Hash s;
						s.add(teams.values.size());
						for (const auto &t : teams.values)
						{
							s.add(t.number);
							s.add(t.mask);
							s.add(t.supplierPresence);
							s.values(t.materials);
							s.values(t.reservedMaterials);
						}
						return s.result();
					}));
	h.add(component(w.entities, 6,
					[](const auto &entities)
					{
						Hash s;
						s.add(entities.buildings.size());
						for (const auto &b : entities.buildings)
						{
							s.ref(b.identity);
							s.add(b.team);
							s.wide(HiringPlanner::configuration(b));
							s.add(b.buildingState);
							s.add(b.constructionResultState);
							s.add(b.desiredMaxUnitWorking);
							s.add(b.maxUnitWorking);
							s.add(b.priority);
							s.add(b.subscriptionWorkingTimer);
							s.wide(b.hiringRecruitmentDeadline);
							s.values(b.wishedMaterials);
							s.values(b.localMaterials);
							s.add(b.usesTeamResources);
							s.add(b.availableSupplyMask);
							static const SimulationSnapshot::FundingState zero{};
							const auto &f =
								b.fundingOffset == SimulationSnapshot::Entities::NoRecord
									? zero
									: entities.funding.at(b.fundingOffset);
							s.values(f.constructionBudget);
							s.values(f.constructionReserved);
							s.values(f.reservedMaterials);
							s.add(b.working.count);
							for (unsigned i = 0; i < b.working.count; ++i)
								s.ref(entities.relationships.at(b.working.offset + i));
						}
						s.add(entities.units.size());
						for (const auto &u : entities.units)
						{
							s.ref(u.identity);
							s.add(u.team);
							s.add(u.typeNum);
							s.add(u.isDead);
							s.add(u.posX);
							s.add(u.posY);
							s.add(u.activity);
							s.add(u.medical);
							s.ref(u.attached);
							s.add(u.carriedMaterial);
							s.add(u.widePrimaryCargo);
							s.add(u.carriedPacket.numerator);
							s.add(u.carriedPacket.denominator);
							s.add(u.movement);
							s.add(u.destinationPurpose);
							s.add(unsigned(u.jobPurpose));
							s.add(u.hp);
							s.add(u.hungry);
							s.add(u.trigHungry);
							s.add(u.hungriness);
							s.add(u.constructionLevel);
							s.add(u.realAttackStrength);
							s.values(u.performance);
							s.values(u.level);
							const auto *cargo = entities.unitCargo.find(u.gid);
							s.add(cargo ? cargo->size() : 0);
							if (cargo)
								for (const auto &entry : *cargo)
								{
									s.add(entry.material);
									s.wide(entry.packet.numerator);
									s.wide(entry.packet.denominator);
								}
						}
						return s.result();
					}));
	// Published planes are ranking inputs. Hashes are calculated alongside
	// the existing snapshot copy, avoiding checksum-time full-plane scans.
	// and hash by logical key, independently of publication container ordering.
	unsigned count = 0;
	for (unsigned key = 0; key < MapState::PlaneCount; ++key)
		if (w.resourceFields->find(key))
			++count;
	h.add(count);
	for (unsigned key = 0; key < MapState::PlaneCount; ++key)
		if (const auto *plane = w.resourceFields->find(key))
		{
			h.add(key);
			h.add(plane->contentChecksum);
		}
	return h.result();
}
