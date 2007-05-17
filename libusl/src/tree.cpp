#include "tree.h"
#include "code.h"

void LazyNode::generate(Definition* def)
{
	value->generate(this->def);
	this->def->body.push_back(new ReturnCode());
	def->body.push_back(new ScopeCode(this->def));
}

void ConstNode::generate(Definition* def)
{
	def->body.push_back(new ConstCode(value));
}

void LocalNode::generate(Definition* def)
{
	def->body.push_back(new LocalCode(depth, local));
}

void ApplyNode::generate(Definition* def)
{
	receiver->generate(def);
	argument->generate(def);
	def->body.push_back(new ApplyCode(name));
}

void ValNode::generate(Definition* def)
{
	value->generate(def);
	def->body.push_back(new ValueCode(local));
}

void BlockNode::generate(Definition* def)
{
	
	for (Statements::const_iterator it = statements.begin(); it != statements.end(); ++it)
	{
		Node* statement = *it;
		statement->generate(def);
		if (dynamic_cast<ExpressionNode*>(statement) != 0)
		{
			// if the statement is an expression, its result is ignored
			def->body.push_back(new PopCode());
		}
	}
	value->generate(def);
}

void TupleNode::generate(Definition* def)
{
	for (Expressions::const_iterator it = expressions.begin(); it != expressions.end(); ++it)
	{
		Node* expression = *it;
		expression->generate(def);
	}
	def->body.push_back(new TupleCode(expressions.size()));
}

