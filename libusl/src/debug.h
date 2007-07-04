#ifndef DEBUG_H
#define DEBUG_H

#include "position.h"

#include <map>
#include <vector>

struct ScopePrototype;

struct ScopeDebugInfo
{
	// maps between program counter's upper bound and source code positions
	typedef std::map<size_t, Position> Address2Source;
	typedef std::map<Position, size_t> Source2Address;
	
	Address2Source address2Source;
	Source2Address source2Address;
	
	const Position& find(size_t address) const;
};

struct FileDebugInfo
{
	typedef std::map<ScopePrototype*, ScopeDebugInfo> Scopes;
	
	Scopes scopes;
	
	ScopeDebugInfo* get(ScopePrototype* scope) { return &scopes[scope]; }
	const Position& find(ScopePrototype* scope, size_t address);
};

struct ProgramDebugInfo
{
	typedef std::map<std::string, FileDebugInfo> Files;
	typedef std::map<ScopePrototype*, Files::iterator> Scopes;
	
	Files files;
	Scopes scopes;
	
	FileDebugInfo* get(const std::string& file) { return &files[file]; }
	FilePosition find(ScopePrototype* scope, size_t address);
	void buildScopes();
};

#endif // ndef DEBUG_H
