#include "lexer.h"
#include "tree.h"
#include "code.h"
#include "memory.h"
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

struct Class: UserMethod
{
	vector<string> fields;
};


struct Object: Scope
{
	Object(Heap* heap, Class* klass, Scope* parent):
		Scope(heap, klass, parent)
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
			Thread::Frame::Stack& stack = thread->frames.back().stack;
			size_t stackSize = stack.size();
			
			Integer* thatInt = dynamic_cast<Integer*>(stack[--stackSize]);
			Integer* thisInt = dynamic_cast<Integer*>(stack[--stackSize]);
			
			assert(thisInt);
			// todo check thatInt type
			
			stack[stackSize++] = new Integer(thread->heap, thisInt->value + thatInt->value);
			
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
	
	Integer(Heap* heap, int value):
		Value(heap, &integerPrototype),
		value(value)
	{
		this->value = value;
	}
};

Integer::IntegerPrototype Integer::integerPrototype;
Integer::IntegerAdd Integer::integerAdd;


struct LookupNode: ExpressionNode
{
	LookupNode(const string& name):
		name(name)
	{}
	
	void generate(UserMethod* method)
	{
		assert(false);
	}
	
	const string name;
};

struct BlockNode: Node
{
	typedef vector<Node*> Statements;
	
	void generate(UserMethod* method)
	{
		for (Statements::const_iterator it = statements.begin(); it != statements.end(); ++it)
		{
			Node* statement = *it;
			statement->generate(method);
			if (dynamic_cast<ExpressionNode*>(statement) != 0)
			{
				// if the statement is an expression, its result is ignored
				method->body.push_back(new PopCode());
			}
		}
		value->generate(method);
	}
	
	void end(Value* nil)
	{
		if (!statements.empty() && dynamic_cast<ExpressionNode*>(statements.back()) != 0)
		{
			value = statements.back();
			statements.pop_back();
		}
		else
		{
			value = new ConstNode(nil);
		}
	}
	
	Statements statements;
	Node* value;
};

struct Parser: Lexer
{
	Parser(const char* src, Heap* heap):
		Lexer(src),
		heap(heap),
		Nil(new Prototype(heap, 0)),
		nil(new Value(heap, Nil))
	{}
	
	BlockNode* parse()
	{
		return statements();
	}
	
	BlockNode* statements()
	{
		newlines();
		auto_ptr<BlockNode> block(new BlockNode());
		while (true)
		{
			switch (tokenType())
			{
			case END:
			case RBRACE:
				block->end(nil);
				return block.release();
			}
			block->statements.push_back(statement());
			newlines();
		}
	}
	
	Node* statement()
	{
		switch (tokenType())
		{
		case VAL:
			{
				next();
				string name = identifier();
				accept(ASSIGN);
				newlines();
				return new ValueNode(name, expression());
			}
		default:
			return expression();
		}
	}
	
	Node* expression()
	{
		auto_ptr<Node> node(simple());
		while (true)
		{
			switch (tokenType())
			{
			case ID:
				node = apply(node, identifier());
				break;
			case LPAR:
				node = apply(node, ".");
				break;
			default:
				return node.release();
			}
		}
	}
	
	auto_ptr<ApplyNode> apply(auto_ptr<Node> receiver, const string& method)
	{
		auto_ptr<ApplyNode> node(new ApplyNode(receiver.release(), method));
		node->args.push_back(simple());
		return node;
	}
	
	Node* simple()
	{
		switch (tokenType())
		{
		case ID:
			{
				return new LookupNode(identifier());
			}
		case NUM:
			{
				string str = token.string();
				next();
				int value = atoi(str.c_str());
				return new ConstNode(new Integer(heap, value));
			}
		case LPAR:
			{
				next();
				// TODO: tuples
				accept(RPAR);
				assert(false);
			}
		case LBRACE:
			{
				next();
				auto_ptr<Node> block(statements());
				accept(RBRACE);
				return block.release();
			}
		default:
			return new ConstNode(nil);
		}
	}
	
	void newlines()
	{
		while (tokenType() == NL)
		{
			next();
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
	
	Heap* heap;
	Prototype* Nil;
	Value* nil;
};

int main(int argc, char** argv)
{
	Heap heap;
	
	Parser parser("val x = 21 + 21\n", &heap);
	Node* node = parser.parse();
/*
	ApplyNode* node = new ApplyNode(new ConstNode(new Integer(2)), "+");
	node->args.push_back(new ConstNode(new Integer(1)));
*/
	
	UserMethod* root = new UserMethod(&heap, 0);
	node->generate(root);
	
	Thread thread(&heap);
	thread.frames.push_back(Thread::Frame(new Scope(&heap, root, 0)));
	
	while (thread.frames.front().nextInstr < root->body.size())
	{
		Thread::Frame& frame = thread.frames.back();
		frame.scope->method()->body[frame.nextInstr++]->execute(&thread);
		cout << "stack size: " << thread.frames.back().stack.size() << endl;
	}
	cout << "x = " << dynamic_cast<Integer*>(thread.frames.back().scope->locals["x"])->value << endl;
	
	thread.frames.pop_back();
	
	cout << "heap size: " << heap.values.size() << "\n";
	cout << "garbage collecting\n";
	heap.garbageCollect(&thread);
	cout << "heap size: " << heap.values.size() << "\n";
}
