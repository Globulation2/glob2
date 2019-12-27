#ifndef TREE_H
#define TREE_H

#include "position.h"

#include <vector>

struct ScopePrototype;
struct ThunkPrototype;
struct Method;
struct DebugInfo;
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

	void generate(ThunkPrototype* scope, DebugInfo* debug, Code* code);
	virtual void generate(ScopePrototype* scope, DebugInfo* debug, Heap* heap) = 0;

	void dump(std::ostream &stream, unsigned indent = 0) const;
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
};

struct ExpressionNode: Node
{
	ExpressionNode(const Position& position):
		Node(position)
	{}

	virtual void generate(ScopePrototype* thunk, DebugInfo* debug, Heap* heap);
	virtual void generate(ThunkPrototype* thunk, DebugInfo* debug, Heap* heap) = 0;
};

struct ConstNode: ExpressionNode
{
	ConstNode(const Position& position, Value* value):
		ExpressionNode(position),
		value(value)
	{}

	virtual void generate(ThunkPrototype* thunk, DebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;

	Value* value;
};

struct SelectNode: ExpressionNode
{
	SelectNode(const Position& position, ExpressionNode* receiver, const std::string& name):
		ExpressionNode(position),
		receiver(receiver),
		name(name)
	{}

	virtual ~SelectNode();
	virtual void generate(ThunkPrototype* thunk, DebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;

	ExpressionNode* receiver;
	const std::string name;
};

struct ApplyNode: ExpressionNode
{
	ApplyNode(const Position& position, ExpressionNode* function, ExpressionNode* argument):
		ExpressionNode(position),
		function(function),
		argument(argument)
	{}

	virtual ~ApplyNode();
	virtual void generate(ThunkPrototype* thunk, DebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;

	ExpressionNode* function;
	ExpressionNode* argument;
};

struct DecNode: Node
{
	enum Type {
		AUTO,
		DEF,
		VAL,
		VAR,
	};

	DecNode(const Position& position, Type type, const std::string& name, ExpressionNode* body):
		Node(position),
		type(type),
		name(name),
		body(body)
	{}

	virtual ~DecNode();
	void declare(ScopePrototype* scope, DebugInfo* debug, Heap* heap);
	virtual void generate(ThunkPrototype* thunk, DebugInfo* debug, Heap* heap);
	virtual void generate(ScopePrototype* scope, DebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;

	Type type;
	std::string name;
	ExpressionNode* body;
};

struct BlockNode: ExpressionNode
{
	typedef std::vector<Node*> Elements;

	BlockNode(const Position& position):
		ExpressionNode(position)
	{}

	virtual ~BlockNode();
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;
	virtual void generate(ThunkPrototype* thunk, DebugInfo* debug, Heap* heap);
	virtual void generateMembers(ScopePrototype* scope, DebugInfo* debug, Heap* heap) = 0;

	Elements elements;
};

struct ExecutionBlock: BlockNode
{
	ExecutionBlock(const Position& position):
		BlockNode(position)
	{}

	virtual void generateMembers(ScopePrototype* scope, DebugInfo* debug, Heap* heap);
};

struct RecordBlock: BlockNode
{
	RecordBlock(const Position& position):
		BlockNode(position)
	{}

	virtual void generateMembers(ScopePrototype* scope, DebugInfo* debug, Heap* heap);
};

struct DefLookupNode: ExpressionNode
{
	DefLookupNode(const Position& position, const std::string& name):
		ExpressionNode(position),
		name(name)
	{}

	virtual void generate(ThunkPrototype* thunk, DebugInfo* debug, Heap* heap);
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

	virtual void generate(ScopePrototype* scope, DebugInfo* debug, Heap* heap);
};

struct NilPatternNode: PatternNode
{
	NilPatternNode(const Position& position):
		PatternNode(position)
	{}

	virtual void generate(ScopePrototype* scope, DebugInfo* debug, Heap* heap);
};

struct ValPatternNode: PatternNode
{
	ValPatternNode(const Position& position, const std::string& name):
		PatternNode(position),
		name(name)
	{}

	virtual void generate(ScopePrototype* scope, DebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;

	std::string name;
};

struct DefPatternNode: PatternNode
{
	DefPatternNode(const Position& position, const std::string& name):
		PatternNode(position),
		name(name)
	{}

	virtual void generate(ScopePrototype* scope, DebugInfo* debug, Heap* heap);
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
	virtual void generate(ScopePrototype* scope, DebugInfo* debug, Heap* heap);
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
	virtual void generate(ThunkPrototype* thunk, DebugInfo* debug, Heap* heap);
	virtual void dumpSpecific(std::ostream &stream, unsigned indent) const;

	PatternNode* arg;
	ExpressionNode* body;
};

#endif // ndef TREE_H
