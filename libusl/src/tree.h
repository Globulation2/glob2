#ifndef TREE_H
#define TREE_H

#include "position.h"

#include <vector>

struct ScopePrototype;
struct Method;
struct FileDebugInfo;
struct Code;
struct Value;
struct Heap;

struct Node
{
	Position position;
	
	Node(const Position& position):
		position(position)
	{}
	
	virtual ~Node() {}
	void generate(ScopePrototype* scope, FileDebugInfo* debug, Code* code);
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap) = 0;
	
	void dump(std::ostream &stream, unsigned indent = 0) const;
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
};

struct ExpressionNode: Node
{
	ExpressionNode(const Position& position):
		Node(position)
	{}
};

struct FunctionNode: ExpressionNode
{
	FunctionNode(const Position& position):
		ExpressionNode(position)
	{}
};

struct ConstNode: ExpressionNode
{
	ConstNode(const Position& position, Value* value):
		ExpressionNode(position),
		value(value)
	{}
	
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	Value* value;
};

struct EvalNode: ExpressionNode
{
	EvalNode(const Position& position, FunctionNode* thunk):
		ExpressionNode(position),
		thunk(thunk)
	{}
	
	virtual ~EvalNode();
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	FunctionNode* thunk;
};

struct SelectNode: FunctionNode
{
	SelectNode(const Position& position, ExpressionNode* receiver, const std::string& name):
		FunctionNode(position),
		receiver(receiver),
		name(name)
	{}
	
	virtual ~SelectNode();
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	ExpressionNode* receiver;
	const std::string name;
};

struct ApplyNode: ExpressionNode
{
	ApplyNode(const Position& position, FunctionNode* function, ExpressionNode* argument):
		ExpressionNode(position),
		function(function),
		argument(argument)
	{}
	
	virtual ~ApplyNode();
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	FunctionNode* function;
	ExpressionNode* argument;
};

struct DefNode: Node
{
	DefNode(const Position& position, const std::string& name, ExpressionNode* body):
		Node(position),
		name(name),
		body(body)
	{}
	
	virtual ~DefNode();
	virtual void define(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	std::string name;
	ExpressionNode* body;
};

struct ValNode: DefNode
{
	ValNode(const Position& position, const std::string& name, ExpressionNode* value):
		DefNode(position, name, 0),
		value(value)
	{}
	
	virtual ~ValNode();
	virtual void define(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	ExpressionNode* value;
};

struct BlockNode: ExpressionNode
{
	typedef std::vector<Node*> Statements;
	
	BlockNode(const Position& position):
		ExpressionNode(position)
	{}
	
	virtual ~BlockNode();
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	Statements statements;
};

struct ArrayNode: ExpressionNode
{
	typedef std::vector<ExpressionNode*> Elements;
	
	ArrayNode(const Position& position):
		ExpressionNode(position)
	{}
	
	virtual ~ArrayNode();
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	Elements elements;
};

struct DefLookupNode: ExpressionNode
{
	DefLookupNode(const Position& position, const std::string& name):
		ExpressionNode(position),
		name(name)
	{}
	
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	std::string name;
};

struct PatternNode: Node
{
	PatternNode(const Position& position):
		Node(position)
	{}
};

struct IgnorePatternNode: PatternNode
{
	IgnorePatternNode(const Position& position):
		PatternNode(position)
	{}
	
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
};

struct NilPatternNode: PatternNode
{
	NilPatternNode(const Position& position):
		PatternNode(position)
	{}
	
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
};

struct ValPatternNode: PatternNode
{
	ValPatternNode(const Position& position, const std::string& name):
		PatternNode(position),
		name(name)
	{}
	
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	std::string name;
};

struct DefPatternNode: PatternNode
{
	DefPatternNode(const Position& position, const std::string& name):
		PatternNode(position),
		name(name)
	{}
	
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	std::string name;
};

struct TuplePatternNode: PatternNode
{
	typedef std::vector<PatternNode*> Members;
	
	TuplePatternNode(const Position& position):
		PatternNode(position)
	{}
	
	virtual ~TuplePatternNode();
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	Members members;
};

struct FunNode: ExpressionNode
{
	FunNode(const Position& position, PatternNode* arg, ExpressionNode* body):
		ExpressionNode(position),
		arg(arg),
		body(body)
	{}
	
	virtual ~FunNode();
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	PatternNode* arg;
	ExpressionNode* body;
};

#endif // ndef TREE_H
