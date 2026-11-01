#include "debug.h"

#include "code.h"
#include "native.h"

using std::string;

const Position& ThunkDebugInfo::find(size_t address) const
{
	Address2Source::const_iterator it = address2Source.upper_bound(address);
	assert(it != address2Source.end());
	return it->second;
}


static string getName(ThunkPrototype* thunk)
{
	for (ThunkPrototype::Body::const_iterator it = thunk->body.begin(); it != thunk->body.end(); ++it)
	{
		NativeCode* nativeCode = dynamic_cast<NativeCode*>(*it);
		if (nativeCode != 0)
		{
			return nativeCode->name;
		}
	}
	
	return "???";
}


const Position& DebugInfo::find(ThunkPrototype* thunk, size_t address)
{
	Thunks::const_iterator it = thunks.find(thunk);
	if (it != thunks.end())
		return it->second.find(address);
	else
	{
		string name = "<";
		name += getName(thunk);
		name += ">";
		Position nativePosition(name, 0, 0);
		ThunkDebugInfo& debug = thunks[thunk];
		size_t lastAddr = thunk->body.size();
		debug.address2Source[lastAddr] = nativePosition;
		debug.source2Address[nativePosition] = lastAddr;
		return debug.address2Source[lastAddr];
	}
	
}

string unmangle(string::const_iterator& begin, string::const_iterator end)
{
	string name;

	if (isdigit(*begin))
	{
		size_t counter(0);
		do
		{
			counter *= 10;
			counter += (*begin - '0');
			++begin;
		}
		while (isdigit(*begin));
	
		for (size_t i = 0; i < counter; ++i)
		{
			name += *begin;
			++begin;
		}
	}
	else {
		switch(*begin)
		{
		case 'i':
			++begin;
			name += "int";
			break;
		case 'b':
			++begin;
			name += "bool";
			break;
		case 'v':
			++begin;
			name += "void";
			break;
		case 'P':
			++begin;
			name += unmangle(begin, end);
			name += '*';
			break;
		case 'F':
			++begin;
			name += unmangle(begin, end);
			name += '(';
			while (*begin != 'E')
				name += unmangle(begin, end);
			++begin;
			name += ')';
			break;
		default:
			name += *begin;
		}
	}
	
	if (begin != end)
	{
		switch(*begin)
		{
		case 'I':
			++begin;
			name += '<';
			while (*begin != 'E')
				name += unmangle(begin, end);
			++begin;
			name += '>';
			break;
		}
	}
	return name;
}

string unmangle(const string& name)
{
	string::const_iterator begin(name.begin());
	return unmangle(begin, name.end());
}
