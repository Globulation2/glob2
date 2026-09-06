// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <Toolkit.h>
#include <FileManager.h>
using namespace GAGCore;
#include <vector>
#include <map>
#include <memory>
#include <sstream>
#include <fstream>
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
	
	/// Parses \a fileName (looked up through the FileManager search path) into
	/// this vector's entries. When \a isDefault is true the parsed block feeds
	/// \ref defaultEntry — the template every later entry is cloned from —
	/// instead of appending a named entry.
	///
	/// Returns true when the file was found and parsed. Returns false, leaving
	/// existing entries untouched, when the file cannot be opened — e.g. an
	/// optional, user-overridable override file (data/nicowar.txt) that is not
	/// present. Callers that require the file (the default block) must treat
	/// false as an error; callers loading an optional override may ignore it.
	/// The Rust port models this return as Result<(), _>.
	bool load(const std::string &fileName, bool isDefault = false)
	{
		bool first = true;
		ConfigBlock b;
		std::string bName;

		// openIFStream returns NULL when the file is absent. Guard it: an
		// unchecked deref here was a release-build crash (the assert that used
		// to stand in was compiled out with NDEBUG). unique_ptr also frees the
		// stream on every exit path, replacing the lone happy-path delete.
		std::unique_ptr<std::ifstream> stream(Toolkit::getFileManager()->openIFStream(fileName));
		if (!stream)
		{
			std::cerr << "ConfigVector::load(" << fileName << ") : file not found" << std::endl;
			return false;
		}

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

		return true;
	}

	bool loadDefault(const std::string &fileName) { return load(fileName, true); }
	
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
