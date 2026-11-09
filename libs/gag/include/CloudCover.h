// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace GAGCore
{
//! One frame of the opening cloud bank (IntroCover) for a renderer that draws
//! it per pixel. Two layers, near and far, each a noise lattice that a
//! threshold carves into cloud, lit from the upper left, casting a shadow down
//! and to the right; then a darkening behind the title. Screen positions are in
//! the drawing coordinates of the full screen.
struct CloudCoverFrame
{
	//! latticeW x latticeH points, four channels each in [0, 65535]: near rank,
	//! near height, far rank, far height. Point (i, j) sits at
	//! (originX + i * cell, originY + j * cell) and values between points are
	//! interpolated linearly, clamped at the edges.
	const uint16_t *lattice = nullptr;
	int latticeW = 0, latticeH = 0;
	float originX = 0, originY = 0, cell = 1;
	float width = 0, height = 0;

	//! The threshold rises from the screen's centre outwards by `bias`.
	float centreX = 0, centreY = 0, maxDistance = 1;
	//! Where a point's shadow falls, and the light's direction for shading.
	float shadowX = 0, shadowY = 0, lightX = 0, lightY = 0;

	struct Layer
	{
		float threshold = 0, bias = 0; //!< threshold at the screen's corners, and how much lower at its centre
		float opacity = 0;             //!< of the densest cloud
		float haze = 0;                //!< how far the cloud's colour is greyed towards the sky
		float shadow = 0;              //!< opacity of the shadow under the densest cloud
	};
	Layer nearLayer, farLayer;

	//! The title's shade: an ellipse of radii (shadeRX, shadeRY) around (shadeX, shadeY).
	float shadeAlpha = 0, shadeX = 0, shadeY = 0, shadeRX = 1, shadeRY = 1;
};
}
