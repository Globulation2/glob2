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
#include "bytecode.h"
#include "interpreter.h"

using namespace std;

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
		
		size_t execute(size_t returnAddress, Thread* thread)
		{
			// check number of arguments
			Frame::Stack& stack = thread->frames.top().stack;
			size_t stackSize = stack.size();
			
			Integer* thatInt = dynamic_cast<Integer*>(stack[--stackSize]);
			Integer* thisInt = dynamic_cast<Integer*>(stack[--stackSize]);
			
			assert(thisInt);
			// todo check thatInt type
			
			stack[stackSize++] = new Integer(thisInt->value + thatInt->value);
			
			stack.resize(stackSize);
			
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
		
		void execute(Scope* scope, Thread* thread)
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


typedef std::vector<Bytecode*> BytecodeVector;

struct Node
{
	virtual ~Node() {}
	virtual void codeGen(BytecodeVector *bytecodes) = 0;
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
	
	Thread t;
	t.frames.push(Frame());
	size_t programCounter = 0;
	
	while (programCounter < bytecode.size())
	{
		programCounter = bytecode[programCounter]->execute(programCounter, &t);
		cout<< "size: " << t.frames.top().stack.size() << endl;
		if (t.frames.top().stack.size())
			cout << "top:  " << dynamic_cast<Integer*>(t.frames.top().stack[0])->value << endl;
	}
}
