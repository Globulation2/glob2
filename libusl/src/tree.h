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

struct FunctionNode: Node
{
	FunctionNode(const Position& position):
		Node(position)
	{}
};

struct ConstNode: Node
{
	ConstNode(const Position& position, Value* value):
		Node(position),
		value(value)
	{}
	
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	Value* value;
};

struct EvalNode: Node
{
	EvalNode(const Position& position, FunctionNode* thunk):
		Node(position),
		thunk(thunk)
	{}
	
	virtual ~EvalNode();
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	FunctionNode* thunk;
};

struct SelectNode: FunctionNode
{
	SelectNode(const Position& position, Node* receiver, const std::string& name):
		FunctionNode(position),
		receiver(receiver),
		name(name)
	{}
	
	virtual ~SelectNode();
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	Node* receiver;
	const std::string name;
};

struct ApplyNode: Node
{
	ApplyNode(const Position& position, FunctionNode* function, Node* argument):
		Node(position),
		function(function),
		argument(argument)
	{}
	
	virtual ~ApplyNode();
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	FunctionNode* function;
	Node* argument;
};

struct DecNode: Node
{
	enum Type {
		AUTO,
		DEF,
		VAL,
		VAR,
	};
	
	DecNode(const Position& position, Type type, const std::string& name, Node* body):
		Node(position),
		type(type),
		name(name),
		body(body)
	{}
	
	virtual ~DecNode();
	void declare(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	Type type;
	std::string name;
	Node* body;
};

struct BlockNode: Node
{
	typedef std::vector<Node*> Elements;
	
	BlockNode(const Position& position):
		Node(position)
	{}
	
	virtual ~BlockNode();
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	Elements elements;
};

struct ExecutionBlock: BlockNode
{
	ExecutionBlock(const Position& position):
		BlockNode(position)
	{}
	
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
};

struct RecordBlock: BlockNode
{
	RecordBlock(const Position& position):
		BlockNode(position)
	{}
	
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
};

struct DefLookupNode: Node
{
	DefLookupNode(const Position& position, const std::string& name):
		Node(position),
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

struct FunNode: Node
{
	FunNode(const Position& position, PatternNode* arg, Node* body):
		Node(position),
		arg(arg),
		body(body)
	{}
	
	virtual ~FunNode();
	virtual void generate(ScopePrototype* scope, FileDebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	
	PatternNode* arg;
	Node* body;
};

#endif // ndef TREE_H
