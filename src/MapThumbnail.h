// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef MapThumbnail_h
#define MapThumbnail_h

#include <string>
#include "SDL_net.h"

namespace GAGCore
{
	class DrawableSurface;
	class OutputStream;
	class InputStream;
};

///This class encapsulates everything about map thumbnails, which are shown when choosing a map before you start a game
class MapThumbnail
{
public:
	///Constructs a thumbnail
	MapThumbnail();
	
	///Loads the thumbnail from the map with the given map name
	void loadFromMap(const std::string& map);
	
	///Encodes this thumbnail into a stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this thumbnail from a stream
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);

	///Loads the picture into the given surface
	void loadIntoSurface(GAGCore::DrawableSurface *surface);
	
	///Returns the map width
	int getMapWidth();
	
	///Returns the map height
	int getMapHeight();
	
	///Returns true if this thumbnail is laoded
	bool isLoaded();

private:
	Uint8 buffer[128 * 128 * 3];
	bool loaded;
	int lastW;
	int lastH;
};

#endif
