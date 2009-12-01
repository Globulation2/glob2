/*
  Copyright (C) 2001-2008 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __GUIPROGRESS_BAR_H
#define __GUIPROGRESS_BAR_H

#include "GUIBase.h"
#include "GraphicContext.h"

namespace GAGGUI
{
	//! This widget displays a progress bar
	class ProgressBar: public RectangularWidget
	{
	protected:
		int value; //!< current value, between 0 and range
		int range; //!< value should be between 0 and range
		std::string font; //!< the name of the used font
		std::string format; //!< the format used for text replacement
		
		GAGCore::Font *fontPtr; //!< pointer to font, this is a cache
		
	public:
		ProgressBar(int x, int y, int w, Uint32 hAlign, Uint32 vAlign, int range = 100, int value = 0, const char* font = 0, std::string format = "%0");
		virtual ~ProgressBar() { }
		
		void setValue(int value) { this->value = value; }
		
		virtual void internalInit(void);
		virtual void paint(void);
	};
}

#endif
