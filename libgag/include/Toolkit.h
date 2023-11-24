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

#ifndef __TOOLKIT_H
#define __TOOLKIT_H

#include <string>
#include <map>

namespace GAGCore
{
	class Sprite;
	class Font;
	class FileManager;
	class StringTable;
	class GraphicContext;
	
	//! Toolkit is a resource server
	class Toolkit
	{
	private:
		// Private constructor, we do not want the user to create a Toolkit, it is a static thing
		Toolkit() { }
		
	public:
		//! Initialize gag, must be called before any call to GAG
		static void init(const char *gameName);
		//! Close gag, must be called after any call to GAG
		static void close(void);
		
		#ifndef YOG_SERVER_ONLY
		//! Initialize the graphic part
		static GraphicContext *initGraphic(int w, int h, unsigned int flags, const std::string title = "", const std::string icon = "");
		
		
		static Sprite *getSprite(const std::string name);
		static void releaseSprite(const std::string name);
		
		static void loadFont(const std::string filename, unsigned size, const std::string name);
		static Font *getFont(const std::string name);
		static void releaseFont(const std::string name);
		
		#endif
		static FileManager *getFileManager(void) { return fileManager; }
		static StringTable *const getStringTable(void) { return strings; }
		
	protected:
		#ifndef YOG_SERVER_ONLY
		friend class Sprite;
		
		typedef std::map<std::string, Sprite *> SpriteMap;
		typedef std::map<std::string, Font *> FontMap;
		
		//! All loaded sprites
		static SpriteMap spriteMap;
		//! All loaded fonts
		static FontMap fontMap;
		//! The actual graphic context
		static GraphicContext *gc;
		#endif
		//! The virtual file system
		static FileManager *fileManager;
		//! The table of strings
		static StringTable *strings;
	};
}

#endif
 
