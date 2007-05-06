#include <cassert>
#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <iterator>
#include <algorithm>
#include <functional>
#include <ext/functional>
#include <utility>
#include <stack>
#include "lexer.h"

using namespace std;

struct Prototype;
struct Object;
struct Scope;
struct Value;
struct Method;

typedef map<string, Value*> Locals;

struct Frame
{
	Scope* scope;
	vector<Value*> exprStack;
	size_t returnAddress;
	
	Frame()
	{
		scope = 0;
		returnAddress = 0;
	}
};

typedef stack<Frame> ExecutionStack;

struct UnknownPrototypeException
{
	string className;
	string PrototypeName;
	
	UnknownPrototypeException(string className, string PrototypeName) :
		className(className),
		PrototypeName(PrototypeName)
	{
	}
};

struct Value
{
	Prototype* proto;
	
	Value(Prototype* proto):
		proto(proto)
	{
	}
	
	virtual ~Value() { }
};


struct Executable
{
	vector<string> args;
	virtual size_t execute(size_t programCounter, ExecutionStack* stack) = 0;
	virtual ~Executable() {}
};

struct Prototype: Value
{
	typedef map<string, Executable*> Methods;
	Prototype* parent;
	Methods methods;
	
	Prototype(Prototype* parent):
		Value(0), parent(parent)
	{ // TODO: MetaPrototype
	}
	
	Executable *lookup(const string &method)
	{
		Methods::const_iterator methodIt = methods.find(method);
		if (methodIt == methods.end())
		{
			if (parent)
				return parent->lookup(method);
			else
				assert(false); // TODO
				//throw UnknownMethodException(method);
		}
		else
			return methodIt->second;
	}
};

struct Method: Prototype, Executable
{
	size_t address;
	
	Method(Prototype* parent):
		Prototype(parent)
	{ }
	
	
	size_t execute(size_t returnAddress, ExecutionStack* stack);
};

struct Scope: Value
{
	Value* parent;
	map<string, Value*> locals;
	
	Scope(Method* method, Value* parent):
		Value(method), parent(parent)
	{
	}
};

size_t Method::execute(size_t returnAddress, ExecutionStack* stack)
{
	// build new frame
	Frame& caller = stack->top();
	stack->push(Frame());
	Frame& frame = stack->top();
	
	// fill frame
	size_t callerArg = caller.exprStack.size();
	frame.scope = new Scope(this, 0);
	frame.returnAddress = returnAddress;
	
	// fetch arguments
	for (size_t i = 0; i < args.size(); i++)
		frame.scope->locals[args[i]] = caller.exprStack[--callerArg];
	frame.scope->parent = caller.exprStack[--callerArg];
	caller.exprStack.resize(callerArg);
	
	// we have to jump to the method
	return address;
}

struct Class: Method
{
	vector<string> fields;
	// TODO: MetaClass
};


struct Object: Scope
{
	Object(Class* klass, Scope* parent):
		Scope(klass, parent)
	{
		transform(klass->fields.begin(), klass->fields.end(), inserter(locals, locals.begin()), bind2nd(ptr_fun(make_pair<string, Value*>), 0));
	}
};

struct Integer: Value
{
	int value;
	static struct IntegerAdd: Executable
	{
		IntegerAdd()
		{
			args.push_back("that");
		}
		
		size_t execute(size_t returnAddress, ExecutionStack* stack)
		{
			// check number of arguments
			Frame& caller = stack->top();
			size_t callerArg = caller.exprStack.size();
			
			Integer* thatInt = dynamic_cast<Integer*>(caller.exprStack[--callerArg]);
			Integer* thisInt = dynamic_cast<Integer*>(caller.exprStack[--callerArg]);
			
			assert(thisInt);
			// todo check thatInt type
			
			caller.exprStack[callerArg++] = new Integer(thisInt->value + thatInt->value);
			
			caller.exprStack.resize(callerArg);
			
			return returnAddress;
		}
	} integerAdd;
	
	static struct IntegerPrototype: Prototype
	{
		IntegerPrototype():
			Prototype(0)
		{
			methods["+"] = &integerAdd;
		}
		
		void execute(Scope* scope, ExecutionStack *stack)
		{
			assert(false);
		}
	} integerPrototype;
	
	Integer(int value): Value(&integerPrototype)
	{
		this->value = value;
	}
};


Integer::IntegerPrototype Integer::integerPrototype;
Integer::IntegerAdd Integer::integerAdd;


struct Bytecode
{
	virtual ~Bytecode() { }
	virtual size_t execute(size_t programCounter, ExecutionStack* stack) = 0;
};

typedef vector<Bytecode*> BytecodeVector;

struct Node
{
	virtual ~Node() {}
	virtual void codeGen(BytecodeVector *bytecodes) = 0;
};


struct ConstBytecode: Bytecode
{
	ConstBytecode(Value *value):
		value(value)
	{}
	
	size_t execute(size_t programCounter, ExecutionStack* stack)
	{
		stack->top().exprStack.push_back(value);
		return programCounter + 1;
	}
	
	Value *value;
};

struct ConstNode: Node
{
	ConstNode(Value *value):
		value(value)
	{}
	
	void codeGen(BytecodeVector *bytecodes)
	{
		bytecodes->push_back(new ConstBytecode(value));
	}
	
	Value *value;
};


struct ApplyBytecode: Bytecode
{
	ApplyBytecode(const string &method, size_t argCount):
		method(method),
		argCount(argCount)
	{}
	
	size_t execute(size_t programCounter, ExecutionStack* stack)
	{
		// fetch receiver
		Value *receiver = *(stack->top().exprStack.end() - argCount - 1);
		
		// fetch method
		Executable *executable = receiver->proto->lookup(method);
		assert(argCount == executable->args.size());
		
		return executable->execute(programCounter + 1, stack);
	}
	
	const string method;
	size_t argCount;
};

struct ApplyNode: Node
{
	ApplyNode(Node *receiver, const string &method):
		receiver(receiver),
		method(method)
	{}
	
	void codeGen(BytecodeVector *bytecodes)
	{
		receiver->codeGen(bytecodes);
		for (size_t i = 0; i < args.size(); i++)
			args[i]->codeGen(bytecodes);
		bytecodes->push_back(new ApplyBytecode(method, args.size()));
	}
	
	Node *receiver;
	const string method;
	vector<Node*> args;
};


/*
class Parser
{
	Parser()
	{
		const char* src = "1 + 1";
		Lexer lexer(src);
	}
	
	Node *parseMethodCall(const string &method)
	{
		auto_ptr<ApplyNode> node(new ApplyNode(method));
		lexer.next();
		while (lexer.peekType() != Lexer::RPAR)
		{
			Node *child = parseExpression();
			assert(child);
			node->args.push_back(child);
			
			assert(lexer.peekType() == Lexer::COMMA);
			lexer.next();
		}
		lexer.next();
		return node.release();
	}
	
	Node *parseExpression()
	{
		switch (lexer.peekType())
		{
			case Lexer::ID:
				string id = lexer.peek().text;
				lexer.next();
				if (lexer.peekType() == Lexer::LPAR)
				{
					parseMethodCall(id);
				}
				else
				{
					parseMemberRef(id);
					// create memeber ref node
					parse();
				}
				return;
			
			case Lexer::NUM:
				int value = atoi(lexer.peek().text.c_str());
				return new ConstNode(new Integer(value));	// todo insert into gc
				
			default:
				return;
		}
	}
};
*/


int main(int argc, char** argv)
{
	//parse();
	
	ApplyNode *p = new ApplyNode(new ConstNode(new Integer(1)), "+");
	p->args.push_back(new ConstNode(new Integer(1)));
	
	BytecodeVector bytecode;
	p->codeGen(&bytecode);
	
	ExecutionStack s;
	s.push(Frame());
	size_t programCounter = 0;
	
	while (programCounter < bytecode.size())
	{
		programCounter = bytecode[programCounter]->execute(programCounter, &s);
		cout << s.top().exprStack.size() << endl;
		if (s.top().exprStack.size())
			cout << "top:" << dynamic_cast<Integer*>(s.top().exprStack[0])->value << endl;
	}
}
