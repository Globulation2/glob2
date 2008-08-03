#ifndef CODE_H
#define CODE_H

#include <cassert>
#include <ostream>
#include "types.h"

class Thread;
class Value;
class ScopePrototype;
class Prototype;
class Operation;


ThunkPrototype* thisMember(Prototype* outer);
ScopePrototype* getMember(Prototype* outer);
ThunkPrototype* nativeMethodMember(NativeMethod* method);


struct Code
{
	virtual ~Code() { }
	virtual void execute(Thread* thread) = 0;
	void dump(std::ostream &stream) const;
	virtual void dumpSpecific(std::ostream &stream) const {};
};

struct ConstCode: Code
{
	ConstCode(Value* value);
	
	virtual void execute(Thread* thread);
	virtual void dumpSpecific(std::ostream &stream) const;
	
	Value* value;
};

struct ValRefCode: Code
{
	ValRefCode(size_t index);
	
	virtual void execute(Thread* thread);
	virtual void dumpSpecific(std::ostream &stream) const;
	
	size_t index;
};

struct EvalCode: Code
{
	virtual void execute(Thread* thread);
};

struct SelectCode: Code
{
	SelectCode(const std::string& name);
	
	virtual void execute(Thread* thread);
	virtual void dumpSpecific(std::ostream &stream) const;
	
	std::string name;
};

struct ApplyCode: Code
{
	virtual void execute(Thread* thread);
};

struct ValCode: Code
{
	ValCode(size_t index);

	virtual void execute(Thread* thread);
	virtual void dumpSpecific(std::ostream &stream) const;
	
	size_t index;
};

struct ParentCode: Code
{
	virtual void execute(Thread* thread);
};

struct PopCode: Code
{
	virtual void execute(Thread* thread);
};

struct DupCode: Code
{
	virtual void execute(Thread* thread);
};

struct ThunkCode: Code
{
	virtual void execute(Thread* thread);
};

struct NativeThunkCode: Code
{
	NativeThunkCode(NativeThunk* thunk);
	
	virtual void execute(Thread* thread);
	virtual void dumpSpecific(std::ostream &stream) const;
	
	NativeThunk* thunk;
};

struct NativeMethodCode: Code
{
	NativeMethodCode(NativeMethod* method);
	
	virtual void execute(Thread* thread);
	virtual void dumpSpecific(std::ostream &stream) const;
	
	NativeMethod* method;
};

template<typename ThunkType>
struct CreateCode: Code
{
	CreateCode(typename ThunkType::Prototype* prototype);
	
	virtual void execute(Thread* thread);
	virtual void dumpSpecific(std::ostream &stream) const;
	
	typename ThunkType::Prototype* prototype;
};

#endif // ndef CODE_H
