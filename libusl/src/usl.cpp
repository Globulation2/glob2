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
		return statements(scope);
	}
	
	BlockNode* statements(ScopePrototype* scope)
	{
		newlines();
		auto_ptr<BlockNode> block(new BlockNode());
		
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
					block->value = new ConstNode(&nil);
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
				next();
				string name = identifier();
				accept(ASSIGN);
				newlines();
				scope->locals.push_back(name);
				return new ValNode(expression(scope));
			}
		case DEF:
			{
				next();
				string name = identifier();
				auto_ptr<ScopePrototype> method(new ScopePrototype(heap, scope));
				auto_ptr<BlockNode> body(new BlockNode());
				switch (tokenType())
				{
				case LPAR:
					next();
					body->statements.push_back(pattern(method.get()));
					accept(RPAR);
					accept(ASSIGN);
					break;
				case ASSIGN:
					next();
					body->statements.push_back(new NilPatternNode());
					break;
				default:
					assert(false);
				}
				newlines();
				scope->methods[name] = method.get();
				body->value = expression(method.get());
				return new DefNode(method.release(), body.release());
			}
		default:
			return expression(scope);
		}
	}
	
	PatternNode* pattern(ScopePrototype* scope)
	{
		PatternNode* first;
		if (tokenType() == LPAR)
		{
			first = pattern(scope);
			accept(RPAR);
		}
		else if (tokenType() == ID)
		{
			string name = identifier();
			scope->locals.push_back(name);
			first = new ValPatternNode(name);
		}
		else
		{
			first = new NilPatternNode();
		}
		
		if (tokenType() != COMMA)
			return first;
		
		auto_ptr<TuplePatternNode> tuple(new TuplePatternNode);
		tuple->members.push_back(first);
		
		do
		{
			next();
			
			if (tokenType() == LPAR)
				tuple->members.push_back(pattern(scope));
			else
			{
				string name = identifier();
				scope->locals.push_back(name);
				tuple->members.push_back(new ValPatternNode(name));
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
				node.reset(selectAndApply(node, identifier(), scope));
				break;
			case LPAR:
			case LBRACE:
			case DOT:
				node.reset(selectAndApply(node, "apply", scope));
				break;
			default:
				return node.release();
			}
		}
	}
	
	ApplyNode* selectAndApply(auto_ptr<ExpressionNode> receiver, const string& method, ScopePrototype* scope)
	{
		FunctionNode* argument = lazyExpr(scope);
		return new ApplyNode(new SelectNode(receiver.release(), method), argument);
	}
	
	ExpressionNode *expressions(ScopePrototype* scope)
	{
		newlines();
		auto_ptr<ArrayNode> tuple(new ArrayNode());
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
				next();
				switch (tuple->elements.size())
				{
				case 0:
					return new ConstNode(&nil);
				case 1:
					{
						ExpressionNode* element = tuple->elements[0];
						tuple->elements.clear();
						return element;
					}
				default:
					return tuple.release();
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
				string name(identifier());
				
				size_t depth = 0;
				do
				{
					ScopePrototype::Locals& locals = scope->locals;
					size_t index = find(locals.begin(), locals.end(), name) - locals.begin();
					if (index < locals.size())
						return new ValRefNode(depth, index);
					scope = dynamic_cast<ScopePrototype*>(scope->outer);
					++depth;
				}
				while (scope != 0);
				
				return new DefLookupNode(name);
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
				auto_ptr<ScopePrototype> block(new ScopePrototype(heap, scope));
				auto_ptr<ExpressionNode> body(statements(block.get()));
				accept(RBRACE);
				return new ApplyNode(new DefRefNode(block.release(), body.release()), 0);
			}
		case DOT:
			next();
			return new ConstNode(&nil);
		default:
			assert(false);
		}
	}
	
	FunctionNode* lazyExpr(ScopePrototype* scope)
	{
		auto_ptr<ScopePrototype> lazy(new ScopePrototype(heap, scope));
		ExpressionNode* expr = simple(lazy.get());
		return new DefRefNode(lazy.release(), expr);
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
	
	ifstream ifs(argv[1]);
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
	
	Parser parser(source.c_str(), &heap);
	Node* node = parser.parse(root);
	node->generate(root);
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
