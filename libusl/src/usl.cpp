#include "debug.h"
#include "lexer.h"
#include "tree.h"
#include "code.h"
#include "memory.h"
#include "interpreter.h"
#include <cassert>
#include <memory>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <functional>
#include <ext/functional>
#include <utility>
#include <stack>

using namespace std;

struct Parser: Lexer
{
	Parser(const char* src, Heap* heap):
		Lexer(src),
		heap(heap)
	{}
	
	BlockNode* parse(ScopePrototype* scope)
	{
		return statements(Position(), scope);
	}
	
	BlockNode* statements(const Position& position, ScopePrototype* scope)
	{
		newlines();
		auto_ptr<BlockNode> block(new BlockNode(position));
		
		scope->methods["this"] = thisMethod(scope);
		
		while (true)
		{
			switch (tokenType())
			{
			case END:
			case RBRACE:
				BlockNode::Statements& statements = block->statements;
				if (!statements.empty())
				{
					block->value = dynamic_cast<ExpressionNode*>(statements.back());
					if (block->value != 0)
					{
						statements.pop_back();
					}
				}
				if (block->value == 0)
				{
					block->value = new ConstNode(token.position, &nil);
				}
				return block.release();
			}
			block->statements.push_back(statement(scope));
			newlines();
		}
	}
	
	Node* statement(ScopePrototype* scope)
	{
		switch (tokenType())
		{
		case VAL:
			{
				Position position = token.position;
				next();
				string name = identifier();
				accept(ASSIGN);
				newlines();
				scope->locals.push_back(name);
				return new ValNode(position, expression(scope));
			}
		case DEF:
			{
				Position position = token.position;
				next();
				string name = identifier();
				auto_ptr<ScopePrototype> method(new ScopePrototype(heap, scope));
				auto_ptr<BlockNode> body(new BlockNode(token.position));
				switch (tokenType())
				{
				case LPAR:
					{
						body->statements.push_back(pattern(method.get()));
						accept(ASSIGN);
					}
					break;
				case ASSIGN:
					body->statements.push_back(new NilPatternNode(token.position));
					next();
					break;
				default:
					assert(false);
				}
				newlines();
				scope->methods[name] = method.get();
				body->value = expression(method.get());
				return new DefNode(position, method.release(), body.release());
			}
		default:
			return expression(scope);
		}
	}
	
	PatternNode* pattern(ScopePrototype* scope)
	{
		Position position = token.position;
		next();
		
		PatternNode* first;
		if (tokenType() == LPAR)
		{
			first = pattern(scope);
		}
		else if (tokenType() == ID)
		{
			Position position = token.position;
			string name = identifier();
			scope->locals.push_back(name);
			first = new ValPatternNode(position, name);
		}
		else
		{
			first = new NilPatternNode(token.position);
		}
		
		if (tokenType() != COMMA)
		{
			accept(RPAR);
			return first;
		}
		
		auto_ptr<TuplePatternNode> tuple(new TuplePatternNode(position));
		tuple->members.push_back(first);
		
		do
		{
			next();
			
			if (tokenType() == LPAR)
			{
				tuple->members.push_back(pattern(scope));
			}
			else
			{
				Position position = token.position;
				string name = identifier();
				scope->locals.push_back(name);
				tuple->members.push_back(new ValPatternNode(position, name));
			}
		}
		while (tokenType() == COMMA);
		
		return tuple.release();
	}
	
	ExpressionNode* expression(ScopePrototype* scope)
	{
		auto_ptr<ExpressionNode> node(simple(scope));
		while (true)
		{
			switch (tokenType())
			{
			case ID:
				{
					Position position = token.position;
					node.reset(selectAndApply(position, node, identifier(), scope));
				}
				break;
			case LPAR:
			case LBRACE:
			case DOT:
				{
					Position position = token.position;
					node.reset(selectAndApply(position, node, "apply", scope));
				}
				break;
			default:
				return node.release();
			}
		}
	}
	
	ApplyNode* selectAndApply(const Position& position, auto_ptr<ExpressionNode> receiver, const string& method, ScopePrototype* scope)
	{
		FunctionNode* argument = lazyExpr(scope);
		return new ApplyNode(position, new SelectNode(position, receiver.release(), method), argument);
	}
	
	ExpressionNode* expressions(ScopePrototype* scope)
	{
		Position position = token.position;
		next();
		newlines();
		auto_ptr<ArrayNode> tuple(new ArrayNode(position));
		while (true)
		{
			switch (tokenType())
			{
			case ID:
			case NUM:
			case LPAR:
			case LBRACE:
			case DOT:
				tuple->elements.push_back(lazyExpr(scope));
				newlines();
				break;
			
			case COMMA:
				next();
				newlines();
				break;
			
			case RPAR:
				{
					Position position = token.position;
					next();
					switch (tuple->elements.size())
					{
					case 0:
						return new ConstNode(position, &nil);
					case 1:
						{
							ExpressionNode* element = tuple->elements[0];
							tuple->elements.clear();
							return element;
						}
					default:
						return tuple.release();
					}
				}
			default:
				assert(false);
			}
		}
	}
	
	ExpressionNode* simple(ScopePrototype* scope)
	{
		switch (tokenType())
		{
		case ID:
			{
				Position position = token.position;
				string name(identifier());
				
				size_t depth = 0;
				do
				{
					ScopePrototype::Locals& locals = scope->locals;
					size_t index = find(locals.begin(), locals.end(), name) - locals.begin();
					if (index < locals.size())
						return new ValRefNode(position, depth, index);
					scope = dynamic_cast<ScopePrototype*>(scope->outer);
					++depth;
				}
				while (scope != 0);
				
				return new DefLookupNode(position, name);
			}
		case NUM:
			{
				Position position = token.position;
				string str = token.string();
				next();
				int value = atoi(str.c_str());
				return new ConstNode(position, new Integer(heap, value));
			}
		case LPAR:
			{
				return expressions(scope);
			}
		case LBRACE:
			{
				Position position = token.position;
				next();
				auto_ptr<ScopePrototype> block(new ScopePrototype(heap, scope));
				auto_ptr<ExpressionNode> body(statements(position, block.get()));
				accept(RBRACE);
				return new ApplyNode(position, new DefRefNode(position, block.release(), body.release()), 0);
			}
		case DOT:
			{
				Position position = token.position;
				next();
				return new ConstNode(position, &nil);
			}
		default:
			assert(false);
		}
	}
	
	FunctionNode* lazyExpr(ScopePrototype* scope)
	{
		Position position = token.position;
		auto_ptr<ScopePrototype> lazy(new ScopePrototype(heap, scope));
		ExpressionNode* expr = simple(lazy.get());
		return new DefRefNode(position, lazy.release(), expr);
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
	if (argc != 2)
		return 1;
	
	string file = argv[1];
	
	ifstream ifs(file.c_str());
	if (!ifs.good())
		return 2;
	
	cout << "Testing " << argv[1] << "\n\n";
	
	string source;
	while (true)
	{
		char c = ifs.get();
		if (ifs.eof() || !ifs.good())
			break;
		source += c;
	}
	ifs.close();
	
	cout << "* source:\n" << source << "\n";

	Heap heap;
	ScopePrototype* root = new ScopePrototype(&heap, 0);
	ProgramDebugInfo debug;
	
	Parser parser(source.c_str(), &heap);
	Node* node = parser.parse(root);
	node->generate(root, debug.get(file));
	delete node;
	
	Thread thread(&heap);
	thread.frames.push_back(Thread::Frame(new Scope(&heap, root, 0)));
	
	int instCount = 0;
	while (thread.frames.size() > 1 || thread.frames.front().nextInstr < root->body.size())
	{
		Thread::Frame& frame = thread.frames.back();
		Code* code = frame.scope->def()->body[frame.nextInstr++];
		code->execute(&thread);
		code->dump(cerr);
		cerr << ", frames: " << thread.frames.size();
		cerr << ", stack size: " << thread.frames.back().stack.size();
		cerr << ", locals: " << thread.frames.back().scope->locals.size();
		cerr << endl;
		instCount++;
	}
	cout << "\n\n* result:\n";
	thread.frames.back().stack.back()->dump(cout);
	cout << endl;
	
	thread.frames.pop_back();
	
	cout << "\n* stats:\n";
	cout << "heap size: " << heap.values.size() << "\tinst count: " << instCount << "\n";
	heap.garbageCollect(&thread);
	//cerr << "heap size: " << heap.values.size() << "\n";
}
