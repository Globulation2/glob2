/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#ifndef __CONFIG_FILES_H
#define __CONFIG_FILES_H

#include <vector>
#include <map>
#include <ofstream>

class ConfigBlock;
class ConfigVector;

//! An interface for file that wish to be loaded from config file
struct LoadableFromConfigFile
{
	virtual void loadFromConfigFile(const ConfigBlock *configBlock) = 0;
};

//! A name-value array holder that can assign them to a variable
class ConfigBlock
{
protected:
	friend class ConfigVector;
	typedef std::map<std::string, std::string> StringMap;
	StringMap lines;
	
public:
	template<typename T> load(T &variable, const std::string &name)
	{
		StringMap::const_iterator valueIt = lines.find(name);
		if (valueIt != lines.end())
		{
			std::ostringstream oss(valueIt->second);
			oss >> variable;
		}
	}
};

//! A configuration array holden that contains multiple time the same config type. Used to contain BuildingTypes and UnitTypes for instance
template<LoadableFromConfigFile T>
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
		ConfigBlock b(defaultEntry);
		std::string bName;
		ConfigVector b;
		
		std::ifstream *stream = Toolkit::getFileManager()->openIFSream(fileName);
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
					bName << stream;
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
					std::ofstream variable, value;
					variable << stream;
					value << stream;
					
					if (stream.good())
						b.lines[variable.str()] = value.str();
				}
			}
		}
		
		if (b.lines.size() > 0)
			addBlock(bName, &b, isDefault);
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
			std::cerr << typeid(*this).name() << "::get(" << id << ") : warning : id is not valid, returning default" << std::endl;
			return &defaultEntry;
		}
	}
	
	size_t getIdByName(const std::string &name) { return nameToEntries[name]; }
	
	const std::string getNameById(size_t id) { return entriesToName[id]; }
};

#endif
