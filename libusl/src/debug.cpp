#include "debug.h"

#include "code.h"

static const Position invalidPosition;
static const FilePosition invalidFilePosition("???", invalidPosition);

const Position& ScopeDebugInfo::find(size_t address) const
{
	Address2Source::const_iterator it = address2Source.upper_bound(address);
	if (it != address2Source.end())
		return it->second;
	else
		return invalidPosition;
}


const Position& FileDebugInfo::find(ScopePrototype* scope, size_t address)
{
	Scopes::const_iterator it = scopes.find(scope);
	if (it != scopes.end())
		return it->second.find(address);
	else
		return invalidPosition;
}


FilePosition ProgramDebugInfo::find(ScopePrototype* scope, size_t address)
{
	if (scopes.size() == 0)
		buildScopes();
	
	Scopes::iterator scopeIt = scopes.find(scope);
	if (scopeIt != scopes.end())
	{
		Files::iterator fileIt = scopeIt->second;
		const std::string& file = fileIt->first;
		const Position& position = fileIt->second.find(scope, address);
		return FilePosition(file, position);
	}
	else
	{
		NativeCode::Operation* operation = dynamic_cast<NativeCode::Operation*>(scope);
		if (operation != 0)
		{
			std::string name = "<";
			name += operation->name;
			name += ">";
			return FilePosition(name, invalidPosition);
		}
		else
			return invalidFilePosition;
	}
	
}

void ProgramDebugInfo::buildScopes()
{
	for(Files::iterator fileIt = files.begin(); fileIt != files.end(); ++fileIt)
	{
		const FileDebugInfo::Scopes& scopes = fileIt->second.scopes;
		for(FileDebugInfo::Scopes::const_iterator scopeIt = scopes.begin(); scopeIt != scopes.end(); ++scopeIt)
		{
			this->scopes[scopeIt->first] = fileIt;
		}
	}
}
