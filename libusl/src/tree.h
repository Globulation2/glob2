#ifndef TREE_H
#define TREE_H

#include "types.h"

struct Code;

struct Node
{
	virtual ~Node() {}
	virtual void generate(Definition* def) = 0;
};

struct ExpressionNode: Node
{};

struct LazyNode: ExpressionNode
{
	LazyNode(Definition* def, ExpressionNode* value):
		def(def),
		value(value)
	{}
	
	void generate(Definition* def);
	
	Definition* def;
	ExpressionNode* value;
};

struct ConstNode: ExpressionNode
{
	ConstNode(Value* value):
		value(value)
	{}
	
	void generate(Definition* def);
	
	Value* value;
};

struct LocalNode: ExpressionNode
{
	LocalNode(size_t depth, size_t index):
		depth(depth),
		index(index)
	{}
	
	void generate(Definition* def);
	
	size_t depth;
	size_t index;
};

struct ApplyNode: ExpressionNode
{
	ApplyNode(Node* receiver, const std::string& name, Node* argument):
		receiver(receiver),
		name(name),
		argument(argument)
	{}
	
	void generate(Definition* def);
	
	Node* receiver;
	const std::string name;
	Node* argument;
};

struct ValNode: Node
{
	ValNode(const std::string& local, Node* value):
		local(local),
		value(value)
	{}
	
	void generate(Definition* def);
	
	const std::string local;
	Node* value;
};

struct BlockNode: ExpressionNode
{
	typedef std::vector<Node*> Statements;
	
	void generate(Definition* def);
	
	Statements statements;
	Node* value;
};

struct DefNode: Node
{
	DefNode(const std::string& name, ExpressionNode* body):
		name(name),
		body(body)
	{}
	
	void generate(Definition* def);
	
	const std::string name;
	ExpressionNode* body;
};

struct ParentNode: ExpressionNode
{
	void generate(Definition* def);
};

struct TupleNode: ExpressionNode
{
	typedef std::vector<ExpressionNode*> Expressions;
	
	void generate(Definition* def);
	
	Expressions expressions;
};

#endif // ndef TREE_H
