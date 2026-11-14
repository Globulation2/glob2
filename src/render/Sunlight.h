// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

//! The one sun every lit or shadowed thing in the world agrees on; see
//! docs/assets/lighting.md. Directions are in screen space with x to the
//! right, y down and z toward the viewer.
namespace Sunlight
{
	//! Unit direction a shadow falls across the ground: to the lower right,
	//! about 53 degrees below the horizontal. The light comes from the
	//! opposite side, the upper left.
	inline constexpr float shadowX = 0.6f, shadowY = 0.8f;

	//! Ground pixels a cloud's shadow falls from the cloud per unit of cloud
	//! height above the ground (the cloudHeight setting, in hundredths).
	inline constexpr float cloudShadowThrow = 80.f;

	//! Screen pixels a flying unit's ground shadow falls from it, and how dark
	//! it is: lighter than a walker's, softened by the height. The throw is a
	//! little longer than the swarm's longest shadow (about 41 pixels), so
	//! explorers read as flying above the tallest building.
	inline constexpr float flightHeight = 52.f;
	inline constexpr unsigned flightShadowAlpha = 64;

	//! A low, glancing sun from the canonical direction, for specular
	//! highlights that would wash out under the high diffuse sun.
	inline constexpr float glancingX = -shadowX, glancingY = -shadowY, glancingZ = 0.15f;
}
