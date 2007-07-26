/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <StringTable.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <Stream.h>
#include "assert.h"
#include <iostream>
#include <cctype>

#include <SDLGraphicContext.h>

namespace GAGCore
{
	StringTable::StringTable()
	{
		languageCount = 0;
		actLang = 0;
		defaultLang = 0;
	}
	
	
	StringTable::~StringTable()
	{
		for (size_t i=0; i<strings.size(); i++)
			delete strings[i];
	}
	
	/*
		Load, argument is a file listing the key file and translation files
		
		The key file contains all keys, each one on a different line
		A translation file contains pair of key-value for the given translation
	*/
	
	bool StringTable::load(const char *filename)
	{
		std::string keyFile;
		std::vector<std::string> translationFiles;
		InputLineStream *inputLineStream;
		 
		// Read index file
		inputLineStream= new InputLineStream(Toolkit::getFileManager()->openInputStreamBackend(filename));
		if (inputLineStream->isEndOfStream())
		{
			delete inputLineStream;
			return false;
		}
		else
		{
			keyFile = inputLineStream->readLine();
			while (!inputLineStream->isEndOfStream())
			{
				const std::string &s = inputLineStream->readLine();
				if (s != "")
					translationFiles.push_back(s);
			}
			delete inputLineStream;
		}
		
		if (translationFiles.size() == 0)
			return false;
		
		languageCount = translationFiles.size();
		
		// Temporary storage for keys and translations
		std::vector<std::string> keys;
		std::vector<std::map<std::string, std::string> > translations(translationFiles.size());
		
		// Load keys
		inputLineStream = new InputLineStream(Toolkit::getFileManager()->openInputStreamBackend(keyFile));
		if (inputLineStream->isEndOfStream())
		{
			delete inputLineStream;
			return false;
		}
		else
		{
			size_t line = 0;
			while (!inputLineStream->isEndOfStream())
			{
				std::string s = inputLineStream->readLine();
				if (s.length() > 0)
				{
					if ((s.length() < 2) || (s[0] != '[') || (s[s.length()-1] != ']'))
						std::cerr << "StringTable::load(" << keyFile << ") : keys must be in bracket. Invalid key " << s << " at line " << line << " ignored" << std::endl;
					else
						keys.push_back(s);
				}
				line++;
			}
			delete inputLineStream;
		}
		
		// Load translations
		for (size_t i=0; i<translationFiles.size(); i++)
		{
			inputLineStream = new InputLineStream(Toolkit::getFileManager()->openInputStreamBackend(translationFiles[i]));
			if (inputLineStream->isEndOfStream())
			{
				delete inputLineStream;
				return false;
			}
			else
			{
				while (!inputLineStream->isEndOfStream())
				{
					const std::string &key = inputLineStream->readLine();
					const std::string &value = inputLineStream->readLine();
					translations[i][key] = value;
				}
				delete inputLineStream;
			}
		}
		
		// Create entries
		for (size_t i=0; i<keys.size(); i++)
		{
			const std::string &key = keys[i];
			OneStringToken *entry = new OneStringToken;
			
			for (size_t j=0; j<translations.size(); j++)
			{
				std::map<std::string, std::string>::const_iterator it = translations[j].find(key);
				if (it != translations[j].end())
					entry->data.push_back(it->second);
				else
					entry->data.push_back("");
			}
			
			stringAccess[key] = strings.size();
			strings.push_back(entry);
		}
		
		// Check for consistency
		for (std::map<std::string, size_t>::iterator it=stringAccess.begin(); it!=stringAccess.end(); ++it)
		{
			// For each entry...
			bool lcwp=false;
			int baseCount=0;
			const std::string &s = it->first;
			// we check that we only have valid format (from a FormatableString point of view)...
			for (size_t j=0; j<s.length(); j++)
			{
				char c = s[j];
				if (lcwp && c!=' ' && c!='%')
				{
					if (isdigit(c))
						baseCount++;
					else
					{
						std::cerr << "StringTable::load(\"" << filename << "\") : error, consistency : text=(" << s << "), Only %x where x is a number are supported in translations !" << std::endl;
						assert(false);
						return false;
					}
				}
				lcwp=(c=='%');
			}
			// then we are sure that format are correct in all translation
			for (size_t i=0; i<strings[it->second]->data.size(); i++)
			{
				const std::string &s = strings[it->second]->data[i];
				bool lcwp=false;
				int count=0;
				for (size_t j=0; j<s.length(); j++)
				{
					char c=s[j];
					if (lcwp && c!=' ' && c!='%')
					{
						if (isdigit(c))
							count++;
						else
						{
							std::cerr << "StringTable::load(\"" << filename << "\") : error, translation consistency : translation=(" << s << "Only %x where x is a number are supported in translations !" << std::endl;
							assert(false);
							return false;
						}
					}
					lcwp=(c=='%');
				}
				// if not, issue an error message
				if (baseCount!=count && s!="")
				{
					std::cerr << "StringTable::load(\"" << filename << "\") : error, translation : in " << translationFiles[i] << ", text = [" << baseCount << "] (" << it->first << "), translation = [" << count << "] (" << s << "), doesn't match !" << std::endl;
					assert(false);
					return false;
				}
			}
		}
		
		return true;
	}
	
	
	void StringTable::print()
	{
		for (std::map<std::string, size_t>::iterator it=stringAccess.begin(); it!=stringAccess.end(); ++it)
		{
			std::cout << "name = " << it->first << "\n";
			for (size_t i=0; i<strings[it->second]->data.size(); i++)
				std::cout<< "trad[" << i << "] = " << strings[it->second]->data[i] << "\n";
			std::cout << std::endl;
		}
	}
	
	const char *StringTable::getString(const char *stringname) const
	{
		int index=-1;
		if ((actLang < languageCount) && (actLang >= 0))
		{
			std::string key(stringname);
			std::map<std::string, size_t>::const_iterator accessIt = stringAccess.find(key);
			if (accessIt == stringAccess.end())
			{
				std::cerr << "StringTable::getString(\"" << stringname << ", " << index << "\") : error, no such key." << std::endl;
				if(!GAGCore::DrawableSurface::translationPicturesDirectory.empty() && GAGCore::DrawableSurface::wroteTexts.find(std::string(stringname))==GAGCore::DrawableSurface::wroteTexts.end())
					GAGCore::DrawableSurface::texts[std::string(stringname)]=key;
				return stringname;
			}
			else
			{
				int dec = index >= 0 ? index + 1 : 0;
				if (accessIt->second+dec >= strings.size())
				{
					std::cerr << "StringTable::getString(\"" << stringname << ", " << index << "\") : error, index out of bound." << std::endl;
					return "ERROR : INDEX OUT OF BOUND";
				}
				std::string &s = strings[accessIt->second+dec]->data[actLang];
				if (s.length() == 0)
				{
					if(!GAGCore::DrawableSurface::translationPicturesDirectory.empty() && GAGCore::DrawableSurface::wroteTexts.find(std::string(stringname))==GAGCore::DrawableSurface::wroteTexts.end())
						GAGCore::DrawableSurface::texts[strings[accessIt->second+dec]->data[defaultLang]]=key;
					return strings[accessIt->second+dec]->data[defaultLang].c_str();
				}
				else
				{
					if(!GAGCore::DrawableSurface::translationPicturesDirectory.empty() && GAGCore::DrawableSurface::wroteTexts.find(std::string(stringname))==GAGCore::DrawableSurface::wroteTexts.end())
						GAGCore::DrawableSurface::texts[s]=key;
					return s.c_str();
				}
			}
		}
		else
		{
			std::cerr << "StringTable::getString(\"" << stringname << ", " << index << "\") : error, bad language selected." << std::endl;
			return "ERROR, BAD LANG";
		}
	}
	
	bool StringTable::doesStringExist(const char *stringname) const
	{
		std::string key(stringname);
		std::map<std::string, size_t>::const_iterator accessIt = stringAccess.find(key);
		if (accessIt == stringAccess.end())
		{
			return false;
		}
		return true;
	}
	
	const char *StringTable::getStringInLang(const char *stringname, int lang) const
	{
		if ((lang < languageCount) && (lang >= 0))
		{
			std::string key(stringname);
			std::map<std::string, size_t>::const_iterator accessIt = stringAccess.find(key);
			if (accessIt == stringAccess.end())
			{
				std::cerr << "StringTable::getStringInLang(\"" << stringname << ", " << lang << "\") : error, no such key." << std::endl;
				return stringname;
			}
			else
			{
				return strings[accessIt->second]->data[lang].c_str();
			}
		}
		else
		{
			std::cerr << "StringTable::getStringInLang(\"" << stringname << ", " << lang << "\") : error, bad language selected." << std::endl;
			return "ERROR, BAD LANG ID";
		}
	}
}
