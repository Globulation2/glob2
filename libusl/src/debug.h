#ifndef DEBUG_H
#define DEBUG_H

#include "position.h"

#include <map>
#include <vector>

struct ThunkPrototype;

struct ThunkDebugInfo
{
	// maps between program counter's upper bound and source code positions
	typedef std::map<size_t, Position> Address2Source;
	typedef std::map<Position, size_t> Source2Address;
	
	Address2Source address2Source;
	Source2Address source2Address;
	
	const Position& find(size_t address) const;
};

struct DebugInfo
{
	typedef std::map<ThunkPrototype*, ThunkDebugInfo> Thunks;
	
	Thunks thunks;
	
	ThunkDebugInfo* get(ThunkPrototype* thunk) { return &thunks[thunk]; }
	const Position& find(ThunkPrototype* thunk, size_t address);
};

std::string unmangle(const std::string& name);

#endif // ndef DEBUG_H
