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

Value* eager(Thread* thread, Value* lazy) {
	
}

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

struct BlockNode: ExpressionNode
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
	
	Statements statements;
	Node* value;
};

struct Parser: Lexer
{
	Parser(const char* src, Heap* heap):
		Lexer(src),
		heap(heap)
	{}
	
	BlockNode* parse(UserMethod* method)
	{
		auto_ptr<Scope> scope(new Scope(0, method, 0));
		return statements(scope.get());
	}
	
	BlockNode* statements(Scope* scope)
	{
		newlines();
		auto_ptr<BlockNode> block(new BlockNode());
		while (true)
		{
			switch (tokenType())
			{
			case END:
			case RBRACE:
				BlockNode::Statements& statements = block->statements;
				if (!statements.empty() && dynamic_cast<ExpressionNode*>(statements.back()) != 0)
				{
					block->value = statements.back();
					statements.pop_back();
				}
				else
				{
					block->value = new ConstNode(&nil);
				}
				return block.release();
			}
			block->statements.push_back(statement(scope));
			newlines();
		}
	}
	
	Node* statement(Scope* scope)
	{
		switch (tokenType())
		{
		case VAL:
			{
				next();
				string name = identifier();
				accept(ASSIGN);
				newlines();
				scope->locals[name] = &nil;
				return new ValNode(name, expression(scope));
			}
		default:
			return expression(scope);
		}
	}
	
	Node* expression(Scope* scope)
	{
		auto_ptr<ExpressionNode> node(simple(scope));
		while (true)
		{
			switch (tokenType())
			{
			case ID:
				node = apply(scope, node, identifier());
				break;
			case LPAR:
			case LBRACE:
				node = apply(scope, node, ".");
				break;
			default:
				return node.release();
			}
		}
	}
	
	auto_ptr<ApplyNode> apply(Scope* scope, auto_ptr<ExpressionNode> receiver, const string& method)
	{
		auto_ptr<ApplyNode> node(new ApplyNode(receiver.release(), method));
		node->args.push_back(lazy(scope));
		return node;
	}
	
	ExpressionNode* simple(Scope* scope)
	{
		switch (tokenType())
		{
		case ID:
			{
				string name(identifier());
				
				Scope* current(scope);
				size_t depth = 0;
				do
				{
					Value* value = scope->lookup(name);
					if (value != 0)
						return new LocalNode(depth, name);
					current = current->parent;
					++depth;
				}
				while (current != 0);
				
				return new LookupNode(name);
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
				UserMethod* method(new UserMethod(heap, scope->prototype));
				auto_ptr<Scope> newScope(new Scope(0, method, scope));
				auto_ptr<ExpressionNode> body(statements(newScope.get()));
				accept(RBRACE);
				return new ApplyNode(new LazyNode(method, body.release()), ".");
			}
		default:
			return new ConstNode(&nil);
		}
	}
	
	LazyNode* lazy(Scope* scope)
	{
		UserMethod* method(new UserMethod(heap, scope->prototype));
		auto_ptr<Scope> newScope(new Scope(0, method, scope));
		return new LazyNode(method, simple(newScope.get()));
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
};

int main(int argc, char** argv)
{
	Heap heap;
	UserMethod* root = new UserMethod(&heap, 0);
	
	Parser parser("val x = 21 + 21\n", &heap);
	Node* node = parser.parse(root);
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
