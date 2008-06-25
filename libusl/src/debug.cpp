#include "debug.h"

#include "code.h"

using namespace std;

const Position& ThunkDebugInfo::find(size_t address) const
{
	Address2Source::const_iterator it = address2Source.upper_bound(address);
	assert(it != address2Source.end());
	return it->second;
}


static string getName(ThunkPrototype* thunk)
{
	NativeThunk* nativeThunk = dynamic_cast<NativeThunk*>(thunk);
	if (nativeThunk != 0)
		return nativeThunk->name;
	
	NativeMethod* nativeMethod = dynamic_cast<NativeMethod*>(thunk);
	if (nativeMethod != 0)
		return nativeMethod->name;

	for (ThunkPrototype::Body::const_iterator it = thunk->body.begin(); it != thunk->body.end(); ++it)
	{
		CreateCode<Function>* createCode = dynamic_cast<CreateCode<Function>*>(*it);
		if (createCode != 0)
		{
			nativeMethod = dynamic_cast<NativeMethod*>(createCode->prototype);
			return nativeMethod->name + "::getter";
		}
	}
	
	assert(nativeMethod);
	return "";
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

string unmangle(const string& name)
{
	string clear;
	int counter = 0;
	for (string::const_iterator it = name.begin(); it != name.end(); ++it)
	{
		string::value_type c = *it;
		if (isdigit(c))
		{
			counter *= 10;
			counter += (c - '0');
		}
		else if (counter != 0)
		{
			for (int i = 0; i < counter; ++i)
			{
				clear += *it;
				if (i < counter - 1)
					++it;
			}
			counter = 0;
		}
		else if (c == 'I')
		{
			clear += '<';
		}
		else if (c == 'E')
		{
			clear += '>';
		}
		else
		{
			clear += c;
		}
	}
	return clear;
}
