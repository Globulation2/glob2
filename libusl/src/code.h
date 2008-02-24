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


ScopePrototype* thisMember(Prototype* outer);
ScopePrototype* getMember(Prototype* outer);
ScopePrototype* nativeMethodMember(Method* method);


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

struct ApplyCode: EvalCode
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

struct ScopeCode: Code
{
	virtual void execute(Thread* thread);
};

struct ReturnCode: Code
{
	virtual void execute(Thread* thread);
};

struct NativeCode: Code
{
	NativeCode(NativeMethod* method);
	
	virtual void execute(Thread* thread);
	virtual void dumpSpecific(std::ostream &stream) const;
	
	NativeMethod* method;
};

struct DefRefCode: Code
{
	DefRefCode(ScopePrototype* def);
	
	virtual void execute(Thread* thread);
	virtual void dumpSpecific(std::ostream &stream) const;
	
	ScopePrototype* def;
};

struct FunCode: Code
{
	FunCode(Method* method);
	
	virtual void execute(Thread* thread);
	
	Method* method;
};

#endif // ndef CODE_H
