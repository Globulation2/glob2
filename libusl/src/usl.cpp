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
		
		scope->members["this"] = thisMember(scope);
		
		while (true)
		{
			switch (tokenType())
			{
			case END:
			case RBRACE:
				{
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
			default:
				block->statements.push_back(statement(scope));
				newlines();
			}
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
				size_t index = scope->locals.size();
				scope->locals.push_back(name);
				ScopePrototype* getter = new ScopePrototype(heap, scope);
				scope->members[name] = getter;
				accept(ASSIGN);
				newlines();
				return new ValNode(position, expression(scope), index, getter);
			}
		case DEF:
			{
				Position position = token.position;
				next();
				string name = identifier();
				ScopePrototype* def = new ScopePrototype(heap, scope);
				scope->members[name] = def;
				ExpressionNode* body;
				switch (tokenType())
				{
				case LPAR:
					{
						Method* fun = new Method(heap, def);
						Position position = token.position;
						auto_ptr<PatternNode> arg(pattern(fun));
						accept(ASSIGN);
						newlines();
						body = expression(fun);
						body = new FunNode(position, fun, arg.release(), body);
					}
					break;
				case ASSIGN:
					next();
					newlines();
					body = expression(def);
					break;
				default:
					assert(false);
				}
				return new DefNode(position, def, body);
			}
		default:
			return expression(scope);
		}
	}
	
	PatternNode* pattern(ScopePrototype* scope)
	{
		Position position = token.position;
		switch (tokenType())
		{
		case LPAR:
			next();
			if (tokenType() != RPAR)
			{
				auto_ptr<TuplePatternNode> tuple(new TuplePatternNode(position));
				tuple->members.push_back(pattern(scope));
				while (tokenType() == COMMA)
				{
					next();
					tuple->members.push_back(pattern(scope));
				}
				accept(RPAR);
				if (tuple->members.size() > 1)
					return tuple.release();
				else
				{
					PatternNode* p = tuple->members.back();
					tuple->members.pop_back();
					return p;
				}
			}
			else
			{
				return new NilPatternNode(position);
			}
		case ID:
			{
				string name = identifier();
				scope->locals.push_back(name);
				ScopePrototype* getter = new ScopePrototype(heap, scope);
				scope->members[name] = getter;
				return new ValPatternNode(position, name, getter);
			}
		case DEF:
			{
				next();
				string name = identifier();
				scope->locals.push_back(name);
				ScopePrototype* def = new ScopePrototype(heap, scope);
				scope->members[name] = def;
				return new DefPatternNode(position, name, def);
			}
		case WILDCARD:
			next();
			return new IgnorePatternNode(position);
		default:
			assert(false);
		}
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
							return new EvalNode(element->position, dynamic_cast<FunctionNode*>(element));
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
				return new DefLookupNode(token.position, identifier());
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
				return new EvalNode(position, new DefRefNode(position, block.release(), body.release()));
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
			cout << token.position.line << ":" << token.position.column << ": ";
			cout << "expecting " << getType(ID)->desc << ", found " << token.type->desc << "\n";
			assert(false);
		}
	}
	
	void accept(TokenType type)
	{
		if (tokenType() == type)
		{
			next();
		}
		else
		{
			cout << token.position.line << ":" << token.position.column << ": ";
			cout << "expecting " << getType(type)->desc << ", found " << token.type->desc << "\n";
			assert(false);
		}
	}
	
	Heap* heap;
};


void dumpCode(ScopePrototype* scope, FileDebugInfo* debug, ostream& stream)
{
	stream << scope << '\n';
	for (size_t i = 0; i < scope->body.size(); ++i)
	{
		Position pos = debug->find(scope, i);
		stream << pos.line << ":" << pos.column << ": ";
		scope->body[i]->dump(stream);
		stream << '\n';
	}
	stream << '\n';
}

void dumpCode(Heap* heap, FileDebugInfo* debug, ostream& stream)
{
	for (Heap::Values::iterator values = heap->values.begin(); values != heap->values.end(); ++values)
	{
		ScopePrototype* scope = dynamic_cast<ScopePrototype*>(*values);
		if (scope != 0)
			dumpCode(scope, debug, stream);
	}
}


int main(int argc, char** argv)
{
	if (argc != 2)
	{
		cerr << "Wrong number of arguments" << endl;
		return 1;
	}
	
	string file = argv[1];
	
	ifstream ifs(file.c_str());
	if (!ifs.good())
	{
		cerr << "Can't open file " << file << endl;
		return 2;
	}
	
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
	node->dump(cout);
	node->generate(root, debug.get(file));
	delete node;
	
	cout << '\n';
	dumpCode(&heap, debug.get(file), cout);
	
	Thread thread(&heap);
	thread.frames.push_back(Thread::Frame(new Scope(&heap, root, 0)));
	
	int instCount = 0;
	while (thread.frames.size() > 1 || thread.frames.front().nextInstr < root->body.size())
	{
		Thread::Frame& frame = thread.frames.back();
		ScopePrototype* scope = frame.scope->def();
		
		for (size_t i = 1; i < thread.frames.size(); ++i)
			cout << '\t';
		cout << "[" << thread.frames.back().scope->locals.size() << "," << thread.frames.back().stack.size() << "]";
		
		FilePosition position = debug.find(scope, frame.nextInstr);
		cout << " => " << position.file << ":" << position.position.line << ":" << position.position.column << ": ";
		
		Code* code = scope->body[frame.nextInstr++];
		code->dump(cout);
		cout << endl;
		code->execute(&thread);
		
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
