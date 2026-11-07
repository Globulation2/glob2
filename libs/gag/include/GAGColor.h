// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

// GAGCore::Color on its own: simulation, team and event headers name colors without
// pulling in the graphics context, the full SDL umbrella or the renderer.
#include <SDL3/SDL_stdinc.h>

namespace GAGCore
{
	//! Color is 4 bytes big but provides easy access to components
	struct Color
	{
		//! Typical useful alpha values pre-defined
		enum Alpha
		{
			ALPHA_TRANSPARENT = 0, //!< constant for transparent alpha
			ALPHA_OPAQUE = 255 //!< constant for opaque alpha
		};
		
		Uint8 r, g, b, a; //!< component of the color
		
		//! Constructor. Default color is opaque black
		Color() { r = g = b = 0; a = ALPHA_OPAQUE; }
		//! Constructor from components
		Color(Uint8 r, Uint8 g, Uint8 b, Uint8 a = ALPHA_OPAQUE) { this->r = r; this->g = g; this->b = b; this->a = a; }
		
		//! Return HSV values in pointers
		void getHSV(float *hue, float *sat, float *lum);
		//! Set color from HLS, alpha untouched
		void setHSV(float hue, float sat, float lum);
		
		//! pack components in a 32 bits int given SDL screen values
		Uint32 pack() const;
		//! unpack from a 32 bits int given SDL screen values
		void unpack(const Uint32 packedValue);
		
		//! comparaison for inequality
		bool operator<(const Color &o) const { return pack() < o.pack(); }
		//! comparaison for equality
		bool operator==(const Color &o) const { return pack() == o.pack(); }
		
		//! return a new color with a different alpha value
		Color applyAlpha(Uint8 _a) const { Color c = *this; c.a = _a; return c; }
		//! return a new color resulting of the multiplication by alpha
		Color applyMultiplyAlpha(Uint8 _a) const ;
		
		static Color black; //!< black color (0,0,0)
		static Color white; //!< black color (255,255,255)
	};
	
	//! Deprecated, for compatibility only. Eventually, all Color32 should be removed or changed to Color
	typedef Color Color32;
}
