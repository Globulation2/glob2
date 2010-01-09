/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef __GUIMAPPREVIEW_H
#define __GUIMAPPREVIEW_H

#include <GUIBase.h>
#include "MapGenerationDescriptor.h"
#include <string>
#include "MapThumbnail.h"

namespace GAGCore
{
	class DrawableSurface;
}

using namespace GAGGUI;
using namespace GAGCore;

//! Widget to preview map
/*!
	This widget is used to preview map.
	Each time setMapThmubnail is called a map htumbnail is loaded.
	A map thumbnail is a little image generated by the map editor.
*/
class MapPreview: public RectangularWidget
{
public:
	//! Constructor, takes position, alignement and initial map name
	MapPreview(int x, int y, Uint32 hAlign, Uint32 vAlign);
	//! Constructor, takes position, alignement, initial map name and a tooltip
	MapPreview(int x, int y, Uint32 hAlign, Uint32 vAlign, const std::string &tooltip, const std::string &tooltipFont);
	//! Destructor
	virtual ~MapPreview();
	virtual void paint(void);
	//! Reload thumbnail for a new map
	virtual void setMapThumbnail(const std::string& mapName);
	//! Load from a given thumbnail
	virtual void setMapThumbnail(MapThumbnail thumbnail);
	//! Returns last map width
	int getLastWidth(void) { return thumbnail.getMapWidth(); }
	//! Returns last map height
	int getLastHeight(void) { return thumbnail.getMapHeight(); }
	std::string getMethode(void);
	//! Returns true if the thumbnail is laoded, false otherwise
	bool isThumbnailLoaded();
	
protected:
	MapThumbnail thumbnail;
	DrawableSurface* surface;
};

#endif
