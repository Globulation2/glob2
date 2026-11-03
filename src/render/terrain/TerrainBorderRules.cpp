// SPDX-License-Identifier: GPL-3.0-or-later
// Which boundary profile draws the contact between two materials.
#include "TerrainMaterials.h"

namespace TerrainVisual
{
namespace
{
// Version 1 to 3 catalogs: the rougher profile wins, with a stable key tie break
// that does not depend on manifest order.
unsigned roughestProfile(const Catalog &c, MaterialId a, MaterialId b)
{
	const auto &aa = c.materials[a], &bb = c.materials[b];
	const int ra = c.profiles[aa.profile].roughness, rb = c.profiles[bb.profile].roughness;
	return ra > rb ? aa.profile : ra < rb ? bb.profile : (aa.key < bb.key ? aa.profile : bb.profile);
}
// Same-class contacts: the edge profile with the higher precedence shapes the
// border. Equal precedence keeps a stable profile-key order.
unsigned precedentProfile(const Catalog &c, MaterialId a, MaterialId b)
{
	const auto &pa = c.profiles[c.materials[a].profile], &pb = c.profiles[c.materials[b].profile];
	if (pa.precedence != pb.precedence)
		return pa.precedence > pb.precedence ? c.materials[a].profile : c.materials[b].profile;
	return pa.key <= pb.key ? c.materials[a].profile : c.materials[b].profile;
}
} // namespace

unsigned Catalog::profileFor(MaterialId a, MaterialId b) const
{
	const auto lo = std::min(a, b), hi = std::max(a, b);
	for (const auto &p : treatments)
		if (p.a == lo && p.b == hi)
			return p.profile;
	if (version < 4)
		return roughestProfile(*this, a, b);
	// Version 4: border classes are listed strongest first. The stronger class
	// shapes the edge with its own profile, except that a shoreline class (a
	// pooled liquid, which takes the shape of its basin) lets the land draw its
	// coast profile. Within one class the higher precedence wins.
	const unsigned ca = materials[a].borderClass, cb = materials[b].borderClass;
	if (ca == cb)
		return precedentProfile(*this, a, b);
	const auto strong = ca < cb ? a : b;
	const auto weak = ca < cb ? b : a;
	const auto &edge = profiles[materials[strong].profile];
	if (borderClasses[materials[weak].borderClass].shoreline && edge.coast >= 0)
		return unsigned(edge.coast);
	return materials[strong].profile;
}

bool Catalog::contextualFor(MaterialId a, MaterialId b) const
{
	if (a == b)
		return false;
	if (version >= 4)
		return profiles[profileFor(a, b)].shape == BoundaryShape::Contextual;
	// Earlier catalogs: both surfaces must opt in, so an organic default cannot
	// round a road, boardwalk or sharp hazard just because it is rougher.
	const auto contextual = [&](unsigned profile) { return profiles[profile].shape == BoundaryShape::Contextual; };
	return contextual(materials[a].profile) && contextual(materials[b].profile) &&
		   contextual(profileFor(a, b));
}

bool Catalog::dominates(MaterialId a, MaterialId b) const
{
	if (version < 4 || a == b)
		return false;
	const unsigned ca = materials[a].borderClass, cb = materials[b].borderClass;
	if (ca != cb)
		return ca < cb;
	return profiles[materials[a].profile].precedence > profiles[materials[b].profile].precedence;
}
} // namespace TerrainVisual
