// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "SDL.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

///Maps key-action ids to their persisted names and back
class KeyActionTable
{
public:
	explicit KeyActionTable(Uint32 size) : names(size) {}

	void add(Uint32 id, const std::string& name)
	{
		names[id] = name;
		keys[name] = id;
	}

	///Empty for an unknown id
	std::string getName(Uint32 id) const
	{
		if(id < names.size())
			return names[id];
		return std::string();
	}

	///std::nullopt for an unknown name
	std::optional<Uint32> getAction(const std::string& name) const
	{
		std::map<std::string, Uint32>::const_iterator it = keys.find(name);
		if(it == keys.end())
			return std::nullopt;
		return it->second;
	}

private:
	std::vector<std::string> names;
	std::map<std::string, Uint32> keys;
};
