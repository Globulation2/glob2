#ifndef TREE_H
#define TREE_H

#include "types.h"

struct Code;

struct Node
{
	virtual ~Node() {}
	virtual void generate(ScopePrototype* scope) = 0;
};

struct ExpressionNode: Node
{};

struct DefRefNode: ExpressionNode
{
	DefRefNode(ScopePrototype* scope, ExpressionNode* value):
		scope(scope),
		value(value)
	{}
	
	void generate(ScopePrototype* scope);
	
	ScopePrototype* scope;
	ExpressionNode* value;
};

struct ConstNode: ExpressionNode
{
	ConstNode(Value* value):
		value(value)
	{}
	
	void generate(ScopePrototype* scope);
	
	Value* value;
};

struct ValRefNode: ExpressionNode
{
	ValRefNode(size_t depth, size_t index):
		depth(depth),
		index(index)
	{}
	
	void generate(ScopePrototype* scope);
	
	size_t depth;
	size_t index;
};

struct SelectNode: ExpressionNode
{
	SelectNode(ExpressionNode* receiver, const std::string& name):
		receiver(receiver),
		name(name)
	{}
	
	void generate(ScopePrototype* scope);
	
	ExpressionNode* receiver;
	const std::string name;
};

struct ApplyNode: ExpressionNode
{
	ApplyNode(ExpressionNode* receiver, ExpressionNode* argument):
		receiver(receiver),
		argument(argument)
	{}
	
	void generate(ScopePrototype* scope);
	
	ExpressionNode* receiver;
	ExpressionNode* argument;
};

struct ValNode: Node
{
	ValNode(size_t index, Node* value):
		index(index),
		value(value)
	{}
	
	void generate(ScopePrototype* scope);
	
	size_t index;
	Node* value;
};

struct BlockNode: ExpressionNode
{
	typedef std::vector<Node*> Statements;
	
	void generate(ScopePrototype* scope);
	
	Statements statements;
	Node* value;
};

struct DefNode: Node
{
	DefNode(ScopePrototype* scope, ExpressionNode* body):
		scope(scope),
		body(body)
	{}
	
	void generate(ScopePrototype* scope);
	
	ScopePrototype* scope;
	ExpressionNode* body;
};

struct ParentNode: ExpressionNode
{
	void generate(ScopePrototype* scope);
};

struct TupleNode: ExpressionNode
{
	typedef std::vector<ExpressionNode*> Expressions;
	
	void generate(ScopePrototype* scope);
	
	Expressions expressions;
};

#endif // ndef TREE_H
