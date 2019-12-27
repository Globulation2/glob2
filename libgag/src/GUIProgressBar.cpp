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

#include <GUIStyle.h>
#include <GUIProgressBar.h>
#include <stdarg.h>
#include <SupportFunctions.h>
#include <FormatableString.h>
#include <assert.h>
#include <Toolkit.h>
#include <GraphicContext.h>
#include <algorithm>

using namespace GAGCore;

namespace GAGGUI
{
	ProgressBar::ProgressBar(int x, int y, int w, Uint32 hAlign, Uint32 vAlign, int range, int value, const char* font, std::string format)
	{
		this->x = x;
		this->y = y;
		this->w = w;
		this->h = Style::style->getStyleMetric(Style::STYLE_METRIC_PROGRESS_BAR_HEIGHT);
		this->hAlignFlag = hAlign;
		this->vAlignFlag = vAlign;

		this->value = value;
		this->range = range;

		fontPtr = 0;
		if (font)
			this->font = font;
		this->format = format;
	}

	void ProgressBar::internalInit(void)
	{
		if (font.length())
		{
			fontPtr = Toolkit::getFont(font.c_str());
			assert(fontPtr);
		}
	}

	void ProgressBar::paint(void)
	{
		int hDec;
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);

		assert(parent);
		assert(parent->getSurface());

		if (vAlignFlag==ALIGN_FILL)
			hDec=(h-this->h)>>1;
		else
			hDec=0;

		Style::style->drawProgressBar(parent->getSurface(), x, y+hDec, w, value, range);
		if (fontPtr)
		{
			FormatableString text = FormatableString(format).arg((value*100)/range);
			int textW = fontPtr->getStringWidth(text.c_str());
			int textH = fontPtr->getStringHeight(text.c_str());
			parent->getSurface()->drawString(x + ((w-textW) >> 1), y + ((h-textH) >> 1), fontPtr, text);
		}
	}
}
