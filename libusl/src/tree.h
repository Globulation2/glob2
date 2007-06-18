#ifndef TREE_H
#define TREE_H

#include "position.h"
#include "types.h"

struct Code;

struct Node
{
	Position position;
	
	Node(const Position& position):
		position(position)
	{}
	
	virtual ~Node() {}
	virtual void generate(ScopePrototype* scope) = 0;
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

struct DefRefNode: FunctionNode
{
	DefRefNode(const Position& position, ScopePrototype* scope, ExpressionNode* value):
		FunctionNode(position),
		scope(scope),
		value(value)
	{}
	
	virtual ~DefRefNode();
	virtual void generate(ScopePrototype* scope);
	
	ScopePrototype* scope;
	ExpressionNode* value;
};

struct ConstNode: ExpressionNode
{
	ConstNode(const Position& position, Value* value):
		ExpressionNode(position),
		value(value)
	{}
	
	virtual void generate(ScopePrototype* scope);
	
	Value* value;
};

struct ValRefNode: ExpressionNode
{
	ValRefNode(const Position& position, size_t depth, size_t index):
		ExpressionNode(position),
		depth(depth),
		index(index)
	{}
	
	virtual void generate(ScopePrototype* scope);
	
	size_t depth;
	size_t index;
};

struct SelectNode: FunctionNode
{
	SelectNode(const Position& position, ExpressionNode* receiver, const std::string& name):
		FunctionNode(position),
		receiver(receiver),
		name(name)
	{}
	
	virtual ~SelectNode();
	virtual void generate(ScopePrototype* scope);
	
	ExpressionNode* receiver;
	const std::string name;
};

struct ApplyNode: ExpressionNode
{
	ApplyNode(const Position& position, FunctionNode* receiver, ExpressionNode* argument):
		ExpressionNode(position),
		receiver(receiver),
		argument(argument)
	{}
	
	virtual ~ApplyNode();
	virtual void generate(ScopePrototype* scope);
	
	FunctionNode* receiver;
	ExpressionNode* argument;
};

struct ValNode: Node
{
	ValNode(const Position& position, ExpressionNode* value):
		Node(position),
		value(value)
	{}
	
	virtual ~ValNode();
	virtual void generate(ScopePrototype* scope);
	
	ExpressionNode* value;
};

struct ScopeNode: ExpressionNode
{
	ScopeNode(const Position& position):
		ExpressionNode(position)
	{}
	
	virtual void generate(ScopePrototype* scope);
};

struct ParentNode: ScopeNode
{
	ParentNode(const Position& position, ScopeNode* scope):
		ScopeNode(position),
		scope(scope)
	{}
	
	virtual ~ParentNode();
	virtual void generate(ScopePrototype* scope);
	
	ScopeNode* scope;
};

struct BlockNode: ExpressionNode
{
	typedef std::vector<Node*> Statements;
	
	BlockNode(const Position& position):
		ExpressionNode(position)
	{}
	
	virtual ~BlockNode();
	virtual void generate(ScopePrototype* scope);
	
	Statements statements;
	ExpressionNode* value;
};

struct DefNode: Node
{
	DefNode(const Position& position, ScopePrototype* scope, ExpressionNode* body):
		Node(position),
		scope(scope),
		body(body)
	{}
	
	virtual ~DefNode();
	virtual void generate(ScopePrototype* scope);
	
	ScopePrototype* scope;
	ExpressionNode* body;
};

struct ArrayNode: ExpressionNode
{
	typedef std::vector<ExpressionNode*> Elements;
	
	ArrayNode(const Position& position):
		ExpressionNode(position)
	{}
	
	virtual ~ArrayNode();
	virtual void generate(ScopePrototype* scope);
	
	Elements elements;
};

struct DefLookupNode: ExpressionNode
{
	DefLookupNode(const Position& position, const std::string& name):
		ExpressionNode(position),
		name(name)
	{}
	
	virtual void generate(ScopePrototype* scope);
	
	std::string name;
};

struct PatternNode: Node
{
	PatternNode(const Position& position):
		Node(position)
	{}
};

struct NilPatternNode: PatternNode
{
	NilPatternNode(const Position& position):
		PatternNode(position)
	{}
	
	virtual void generate(ScopePrototype* scope);
};

struct ValPatternNode: PatternNode
{
	ValPatternNode(const Position& position, const std::string& name):
		PatternNode(position),
		name(name)
	{}
	
	virtual void generate(ScopePrototype* scope);
	
	std::string name;
};

struct TuplePatternNode: PatternNode
{
	typedef std::vector<PatternNode*> Members;
	
	TuplePatternNode(const Position& position):
		PatternNode(position)
	{}
	
	virtual ~TuplePatternNode();
	virtual void generate(ScopePrototype* scope);
	
	Members members;
};

#endif // ndef TREE_H
