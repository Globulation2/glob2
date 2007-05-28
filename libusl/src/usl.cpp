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

struct Integer: Value
{
	int value;
	static struct IntegerAdd: NativeCode::Operation
	{
		IntegerAdd():
			NativeCode::Operation(&integerPrototype, "Integer::+", false)
		{}
		
		Value* execute(Thread* thread, Value* receiver, Value* argument)
		{
			Integer* thisInt = dynamic_cast<Integer*>(receiver);
			Integer* thatInt = dynamic_cast<Integer*>(argument);
			
			assert(thisInt);
			assert(thatInt);
			
			return new Integer(thread->heap, thisInt->value + thatInt->value);
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
	
	virtual void dumpSpecific(std::ostream &stream) { stream << "= " << value; }
};

Integer::IntegerPrototype Integer::integerPrototype;
Integer::IntegerAdd Integer::integerAdd;


struct LookupNode: ExpressionNode
{
	LookupNode(const string& name):
		name(name)
	{}
	
	void generate(Definition* def)
	{
		assert(false);
	}
	
	const string name;
};

struct Parser: Lexer
{
	Parser(const char* src, Heap* heap):
		Lexer(src),
		heap(heap)
	{}
	
	BlockNode* parse(Definition* def)
	{
		auto_ptr<Scope> scope(new Scope(0, def, 0));
		return statements(scope.get());
	}
	
	BlockNode* statements(Scope* scope)
	{
		newlines();
		auto_ptr<BlockNode> block(new BlockNode());
		
		scope->prototype->methods["this"] = new Definition(heap, scope->prototype);
		block->statements.push_back(new DefNode("this", new ParentNode()));
		
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
				scope->def()->locals.push_back(name);
				return new ValNode(name, expression(scope));
			}
		default:
			return expression(scope);
		}
	}
	
	ExpressionNode* expression(Scope* scope)
	{
		auto_ptr<ExpressionNode> node(simple(scope));
		while (true)
		{
			switch (tokenType())
			{
			case ID:
				node.reset(apply(node, identifier(), scope));
				break;
			case LPAR:
			case LBRACE:
				node.reset(apply(node, ".", scope));
				break;
			default:
				return node.release();
			}
		}
	}
	
	ApplyNode* apply(auto_ptr<ExpressionNode> receiver, const string& method, Scope* scope)
	{
		ExpressionNode* argument = lazy(scope);
		return new ApplyNode(receiver.release(), method, argument);
	}
	
	ExpressionNode *expressions(Scope* scope)
	{
		newlines();
		auto_ptr<TupleNode> tuple(new TupleNode());
		while (true)
		{
			switch (tokenType())
			{
			case ID:
			case NUM:
			case LPAR:
			case LBRACE:
				tuple->expressions.push_back(expression(scope));
				newlines();
				break;
			
			case COMMA:
				next();
				newlines();
				break;
			
			case RPAR:
				next();
				switch (tuple->expressions.size())
				{
				case 0:
					return new ConstNode(&nil);
				case 1:
					{
						ExpressionNode* expr = tuple->expressions[0];
						tuple->expressions.clear();
						return expr;
					}
				default:
					return tuple.release();
				}
			
			default:
				assert(false);
			}
		}
	}
	
	ExpressionNode* simple(Scope* scope)
	{
		switch (tokenType())
		{
		case ID:
			{
				string name(identifier());
				
				Definition* def(scope->def());
				size_t depth = 0;
				do
				{
					Definition::Locals& locals = def->locals;
					size_t index = find(locals.begin(), locals.end(), name) - locals.begin();
					if (index < locals.size())
						return new LocalNode(depth, index);
					def = dynamic_cast<Definition*>(def->parent);
					++depth;
				}
				while (def != 0);
				
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
				return expressions(scope);
			}
		case LBRACE:
			{
				next();
				Definition* def(new Definition(heap, scope->prototype));
				auto_ptr<Scope> newScope(new Scope(0, def, scope));
				auto_ptr<ExpressionNode> body(statements(newScope.get()));
				accept(RBRACE);
				return new ApplyNode(new LazyNode(def, body.release()), ".", new ConstNode(&nil));
			}
		default:
			return new ConstNode(&nil);
		}
	}
	
	LazyNode* lazy(Scope* scope)
	{
		Definition* def(new Definition(heap, scope->prototype));
		auto_ptr<Scope> newScope(new Scope(0, def, scope));
		return new LazyNode(def, simple(newScope.get()));
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
	auto_ptr<Heap> heap(new Heap());
	Definition* root = new Definition(heap.get(), 0);
	
	Parser parser("val x = 21 + 21\nx", heap.get());
	Node* node = parser.parse(root);
	node->generate(root);
	
	Thread thread(heap.get());
	thread.frames.push_back(Thread::Frame(new Scope(heap.get(), root, 0)));
	
	while (thread.frames.front().nextInstr < root->body.size())
	{
		Thread::Frame& frame = thread.frames.back();
		Code* code = frame.scope->def()->body[frame.nextInstr++];
		code->execute(&thread);
		code->dump(cout);
		cout << ", frames: " << thread.frames.size();
		cout << ", stack size: " << thread.frames.back().stack.size();
		cout << ", locals: " << thread.frames.back().scope->locals.size();
		cout << endl;
	}
	cout << "result: ";
	thread.frames.back().stack.back()->dump(cout);
	cout << endl;
	
	thread.frames.pop_back();
	
	cout << "heap size: " << heap->values.size() << "\n";
	cout << "garbage collecting\n";
	heap->garbageCollect(&thread);
	cout << "heap size: " << heap->values.size() << "\n";
}
