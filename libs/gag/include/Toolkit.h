// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <string>
#include <map>

namespace GAGCore
{
	class Sprite;
	class Font;
	class FileManager;
	class StringTable;
	class AssetLoader;
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
		
		//! Initialize the graphic part
		static GraphicContext *initGraphic(int w, int h, unsigned int flags, const std::string title = "", const std::string icon = "");
		
		
		static Sprite *getSprite(const std::string name);
        static void requestSprite(const std::string& name, bool variableAtlas = false);
        static Sprite *findSprite(const std::string& name);
        //! Share a presentation budget across cooperative jobs, sprites and HD reloads.
        //! One indivisible decode/upload may exceed it; zero only queries readiness.
        static bool pollAssets(unsigned budgetMs = 2);
        static unsigned assetProgress();
		static void releaseSprite(const std::string name);
		
		static void loadFont(const std::string filename, unsigned size, const std::string name);
		static Font *getFont(const std::string name);
		static void releaseFont(const std::string name);
		//! Reopen every loaded font from its file, keeping the Font objects; used
		//! when the browser replaces the font file with its CJK version.
		static void reloadFonts(void);
		
		static FileManager *getFileManager(void) { return fileManager; }
		//! Shared CPU asset service, owned and stopped before the graphics backend.
		static AssetLoader &assets();
		static StringTable *const getStringTable(void) { return strings; }
		
	protected:
		friend class Sprite;
		
		typedef std::map<std::string, Sprite *> SpriteMap;
		typedef std::map<std::string, Font *> FontMap;
		
		//! All loaded sprites
		static SpriteMap spriteMap;
		//! All loaded fonts
		static FontMap fontMap;
		//! The actual graphic context
		static GraphicContext *gc;
		//! The virtual file system
		static FileManager *fileManager;
		//! The table of strings
		static StringTable *strings;
	};
}
 
