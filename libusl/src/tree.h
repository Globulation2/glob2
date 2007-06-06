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

struct FunctionNode: ExpressionNode
{};

struct DefRefNode: FunctionNode
{
	DefRefNode(ScopePrototype* scope, ExpressionNode* value):
		scope(scope),
		value(value)
	{}
	
	virtual void generate(ScopePrototype* scope);
	
	ScopePrototype* scope;
	ExpressionNode* value;
};

struct ConstNode: ExpressionNode
{
	ConstNode(Value* value):
		value(value)
	{}
	
	virtual void generate(ScopePrototype* scope);
	
	Value* value;
};

struct ValRefNode: ExpressionNode
{
	ValRefNode(size_t depth, size_t index):
		depth(depth),
		index(index)
	{}
	
	virtual void generate(ScopePrototype* scope);
	
	size_t depth;
	size_t index;
};

struct SelectNode: FunctionNode
{
	SelectNode(ExpressionNode* receiver, const std::string& name):
		receiver(receiver),
		name(name)
	{}
	
	virtual void generate(ScopePrototype* scope);
	
	ExpressionNode* receiver;
	const std::string name;
};

struct ApplyNode: ExpressionNode
{
	ApplyNode(FunctionNode* receiver, ExpressionNode* argument):
		receiver(receiver),
		argument(argument)
	{}
	
	virtual void generate(ScopePrototype* scope);
	
	FunctionNode* receiver;
	ExpressionNode* argument;
};

struct ValNode: Node
{
	ValNode(Node* value):
		value(value)
	{}
	
	virtual void generate(ScopePrototype* scope);
	
	Node* value;
};

struct ScopeNode: ExpressionNode
{
	virtual void generate(ScopePrototype* scope);
};

struct ParentNode: ScopeNode
{
	ParentNode(ScopeNode* scope):
		scope(scope)
	{}
	
	virtual void generate(ScopePrototype* scope);
	
	ScopeNode* scope;
};

struct BlockNode: ExpressionNode
{
	typedef std::vector<Node*> Statements;
	
	virtual void generate(ScopePrototype* scope);
	
	Statements statements;
	Node* value;
};

struct DefNode: Node
{
	DefNode(ScopePrototype* scope, ExpressionNode* body):
		scope(scope),
		body(body)
	{}
	
	virtual void generate(ScopePrototype* scope);
	
	ScopePrototype* scope;
	ExpressionNode* body;
};

struct TupleNode: ExpressionNode
{
	typedef std::vector<ExpressionNode*> Expressions;
	
	virtual void generate(ScopePrototype* scope);
	
	Expressions expressions;
};

struct DefLookupNode: ExpressionNode
{
	DefLookupNode(const std::string& name):
		name(name)
	{}
	
	virtual void generate(ScopePrototype* scope);
	
	std::string name;
};

#endif // ndef TREE_H
