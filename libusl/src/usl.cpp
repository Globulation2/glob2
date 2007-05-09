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
	Object(Thread *thread, Class* klass, Scope* parent):
		Scope(thread, klass, parent)
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
			Method(0, &integerPrototype)
		{
			args.push_back("that");
		}
		
		void execute(Thread* thread)
		{
			// check number of arguments
			Thread::Frame::Stack& stack = thread->topFrame().stack;
			size_t stackSize = stack.size();
			
			Integer* thatInt = dynamic_cast<Integer*>(stack[--stackSize]);
			Integer* thisInt = dynamic_cast<Integer*>(stack[--stackSize]);
			
			assert(thisInt);
			// todo check thatInt type
			
			stack[stackSize++] = new Integer(thread, thisInt->value + thatInt->value);
			
			stack.resize(stackSize);
		}
	} integerAdd;
	
	static struct IntegerPrototype: Prototype
	{
		IntegerPrototype():
			Prototype(0, 0)
		{
			methods["+"] = &integerAdd;
		}
	} integerPrototype;
	
	Integer(Thread *thread, int value):
		Value(thread, &integerPrototype),
		value(value)
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
		Nil(0, 0),
		nil(0, &Nil)
	{}
	
	Node* statement(Scope* scope)
	{
		switch (tokenType())
		{
			case VAL:
			{
				next();
				string name = identifier();
				accept(ASSIGN);
				scope->locals[name] = &nil;
				return new ValueNode(name, expression(scope));
			}
			default:
				return expression(scope);
		}
	}
	
	string identifier()
	{
		if (tokenType() == ID)
		{
			string id = token.string();
			next();
			return id;
		}
		else
		{
			assert(false);
		}
	}
	
	void accept(TokenType type)
	{
		assert(tokenType() == type);
		next();
	}
	
	Node* expression(Scope* scope)
	{
		auto_ptr<Node> node(simple(scope));
		while (true)
		{
			std::string method;
			switch (tokenType())
			{
				case ID:
					method = token.string();
					next();
					break;
				case LPAR:
					method = ".";
					break;
				default:
					return node.release();
			}
			auto_ptr<ApplyNode> apply(new ApplyNode(node.release(), method));
			apply->args.push_back(simple(scope));
			node = apply;
		}
	}
	
	Node* simple(Scope* scope)
	{
		switch (tokenType())
		{
			case ID:
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
			case NUM:
			{
				string str = token.string();
				next();
				int value = atoi(str.c_str());
				return new ConstNode(new Integer(0, value));	// todo insert into gc or in const table, right now this is LEAK
			}
			case LPAR:
				next();
				// TODO
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
		Method(0, 0)	// todo check garbage collectable itude
	{}
	
	void execute(Thread* thread)
	{
		assert(false);
	}
} root;

int main(int argc, char** argv)
{
	Parser parser("val x = 21 + 21");
	Node* node = parser.statement(new Scope(0, &root, 0));	// todo check garbage collectable, this is LEAKY
/*
	ApplyNode* node = new ApplyNode(new ConstNode(new Integer(2)), "+");
	node->args.push_back(new ConstNode(new Integer(1)));
*/
	
	CodeVector code;
	node->generate(&code);
	
	Thread t;
	t.frames.push_back(Thread::Frame(new Scope(&t, &root, 0)));
	
	while (t.topFrame().nextInstr < code.size())
	{
		code[t.topFrame().nextInstr++]->execute(&t);
		cout << "size: " << t.topFrame().stack.size() << endl;
		if (t.topFrame().stack.size() > 0)
			cout << "[0]: " << dynamic_cast<Integer*>(t.topFrame().stack[0])->value << endl;
		if (t.topFrame().stack.size() > 1)
			cout << "[1]: " << dynamic_cast<Integer*>(t.topFrame().stack[1])->value << endl;
	}
	
	cout << "x = " << dynamic_cast<Integer*>(t.topFrame().scope->locals["x"])->value << endl;
	cout << "heap size: " << t.heap.size() << "\n";
	cout << "garbage collecting\n";
	t.garbageCollect();
	cout << "heap size: " << t.heap.size() << "\n";
}
