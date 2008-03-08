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

#ifndef __CONFIG_FILES_H
#define __CONFIG_FILES_H

#include <Toolkit.h>
#include <FileManager.h>
using namespace GAGCore;
#include <vector>
#include <map>
#include <sstream>
#include <iostream>
#include <assert.h>

class ConfigBlock;

//! An interface for file that wish to be loaded from config file
struct LoadableFromConfigFile
{
	virtual void loadFromConfigFile(const ConfigBlock *configBlock) = 0;
	virtual ~LoadableFromConfigFile() {}
};

//! A name-value array holder that can assign them to a variable
class ConfigBlock
{
protected:
	template<typename T>
	friend class ConfigVector;
	typedef std::map<std::string, std::string> StringMap;
	StringMap lines;
	
public:
	template<typename T>
	void load(T &variable, const std::string &name) const
	{
		StringMap::const_iterator valueIt = lines.find(name);
		if (valueIt != lines.end())
		{
			std::istringstream iss(valueIt->second);
			iss >> variable;
		}
	}
};

//! A configuration array holden that contains multiple time the same config type. Used to contain BuildingTypes and UnitTypes for instance
template<typename T>
class ConfigVector
{
protected:
	std::vector<T*> entries;
	std::vector<std::string> entriesToName;
	std::map<std::string, size_t> nameToEntries;
	T defaultEntry;
	
	void addBlock(const std::string &blockName, const ConfigBlock *block, bool isDefault)
	{
		if (isDefault)
		{
			defaultEntry.loadFromConfigFile(block);
		}
		else
		{
			T *c = new T(defaultEntry);
			c->loadFromConfigFile(block);
			size_t id = entries.size();
			entries.push_back(c);
			entriesToName.push_back(blockName);
			nameToEntries[blockName] = id;
		}
	}
	
public:
	~ConfigVector()
	{
		for (size_t i=0; i<entries.size(); ++i)
			delete entries[i];
	}
	
	void load(const std::string &fileName, bool isDefault = false)
	{
		bool first = true;
		ConfigBlock b;
		std::string bName;
		
		std::ifstream *stream = Toolkit::getFileManager()->openIFStream(fileName);
		assert(stream);
		
		while (stream->good())
		{
			int c = stream->get();
			
			switch (c)
			{
				// New block, commit the old if any
				case '*':
				{
					if ((!first) && (b.lines.size() > 0))
					{
						addBlock(bName, &b, isDefault);
						b.lines.clear();
					}
					else
						first = false;
					char temp[256];
					stream->getline(temp, 256);
					bName = temp;
				}
				break;
				
				// Comment, eat one line
				case '#':
				case '/':
				{
					while (stream->good())
					{
						int cc = stream->get();
						if (cc == '\n' || cc == '\r')
							break;
					}
				}
				break;
				
				// new line, ignore
				case '\n':
				case '\r':
				break;
				
				// normal entry, read line, commit only if valid
				default:
				{
					stream->putback(c);
					std::string variable, value;
					*stream >> variable;
					if (stream->bad())
					{
						std::cerr << "ConfigVector::load(" << fileName << ") : error : incomplete entry at end of file" << std::endl;
						assert(false);
					}
					*stream >> value;
					
					b.lines[variable] = value;
				}
			}
		}
		
		if (b.lines.size() > 0)
			addBlock(bName, &b, isDefault);
			
		delete stream;
	}
	
	void loadDefault(const std::string &fileName) { load(fileName, true); }
	
	T* get(size_t id)
	{
		if (id < entries.size())
		{
			return entries[id];
		}
		else
		{
			std::cerr << "ConfigVector::get(" << static_cast<unsigned int>(id) << ") : warning : id is not valid, returning default" << std::endl;
			assert(false);
			return &defaultEntry;
		}
	}
	
	size_t getIdByName(const std::string &name) { return nameToEntries[name]; }
	
	const std::string getNameById(size_t id) { return entriesToName[id]; }
};

#endif
