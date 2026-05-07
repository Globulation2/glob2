// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2008 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "GUIBase.h"
#include "GraphicContext.h"

namespace GAGGUI
{
	//! This widget is a simple image widget
	class Image: public RectangularWidget
	{
	protected:
		GAGCore::DrawableSurface *image; //!< pointer to the image, the widget does not delete it
		
	public:
		Image(int x, int y, Uint32 hAlign, Uint32 vAlign, GAGCore::DrawableSurface *image);
		virtual ~Image() { }
		
		virtual void paint(void);
	};
}
