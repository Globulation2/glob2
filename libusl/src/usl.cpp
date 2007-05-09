#include "lexer.h"
#include "tree.h"
#include "code.h"
#include <cassert>
#include <memory>
#include <vector>
#include <string>
#include <iostream>
#include <iterator>
#include <algorithm>
#include <functional>
#include <ext/functional>
#include <utility>
#include <stack>

using namespace std;

struct Class: Method
{
	vector<string> fields;
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
	static struct IntegerAdd: Method
	{
		IntegerAdd():
			Method(&integerPrototype)
		{
			args.push_back("that");
		}
		
		void execute(Thread* thread)
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
		}
	} integerAdd;
	
	static struct IntegerPrototype: Prototype
	{
		IntegerPrototype():
			Prototype(0)
		{
			methods["+"] = &integerAdd;
		}
	} integerPrototype;
	
	Integer(int value): Value(&integerPrototype)
	{
		this->value = value;
	}
};


Integer::IntegerPrototype Integer::integerPrototype;
Integer::IntegerAdd Integer::integerAdd;


struct Parser: Lexer
{
	Parser(const char* src):
		Lexer(src),
		Nil(0),
		nil(&Nil)
	{ }
	
	Node* applyExpr(Scope* scope)
	{
		auto_ptr<Node> node(simpleExpr(scope));
		while (true)
		{
			std::string method;
			switch (tokenType())
			{
				case Lexer::ID:
					method = token.string();
					next();
					break;
				case Lexer::LPAR:
					method = ".";
					break;
				default:
					return node.release();
			}
			auto_ptr<ApplyNode> apply(new ApplyNode(node.release(), method));
			apply->args.push_back(simpleExpr(scope));
			node = apply;
		}
	}
	
	Node* simpleExpr(Scope* scope)
	{
		switch (tokenType())
		{
			case Lexer::ID:
			{
				const string name(token.string());
				next();
				Value* value = scope->lookup(name);
				if (value != 0)
				{
					return new LocalNode(name);
				}
				else
				{
					assert(false);
				}
			}
			case Lexer::NUM:
			{
				string str = token.string();
				next();
				int value = atoi(str.c_str());
				return new ConstNode(new Integer(value));	// todo insert into gc
			}
			case Lexer::LPAR:
				next();
			default:
				assert(false);
		}
	}
	
	Prototype Nil;
	Value nil;
};

struct Root: Method
{
	Root():
		Method(0)
	{}
	
	void execute(Thread* thread)
	{
		assert(false);
	}
} root;

int main(int argc, char** argv)
{
	Parser parser("21\n+21");
	Node* node = parser.applyExpr(new Scope(&root, 0));
/*
	ApplyNode* node = new ApplyNode(new ConstNode(new Integer(2)), "+");
	node->args.push_back(new ConstNode(new Integer(1)));
*/
	
	CodeVector code;
	node->generate(&code);
	
	Thread t;
	t.frames.push(Frame(new Scope(&root, 0)));
	
	while (t.frames.top().nextInstr < code.size())
	{
		code[t.frames.top().nextInstr++]->execute(&t);
		cout << "size: " << t.frames.top().stack.size() << endl;
		if (t.frames.top().stack.size() > 0)
			cout << "[0]: " << dynamic_cast<Integer*>(t.frames.top().stack[0])->value << endl;
		if (t.frames.top().stack.size() > 1)
			cout << "[1]: " << dynamic_cast<Integer*>(t.frames.top().stack[1])->value << endl;
	}
}
