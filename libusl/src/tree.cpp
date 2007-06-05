#include "tree.h"
#include "code.h"

void DefRefNode::generate(ScopePrototype* scope)
{
	value->generate(this->scope);
	this->scope->body.push_back(new ReturnCode());
	scope->body.push_back(new ScopeCode(this->scope));
}

void ConstNode::generate(ScopePrototype* scope)
{
	scope->body.push_back(new ConstCode(value));
}

void ValRefNode::generate(ScopePrototype* scope)
{
	scope->body.push_back(new ValRefCode(depth, index));
}

void SelectNode::generate(ScopePrototype* scope)
{
	receiver->generate(scope);
	scope->body.push_back(new SelectCode(name));
}

void ApplyNode::generate(ScopePrototype* scope)
{
	receiver->generate(scope);
	argument->generate(scope);
	scope->body.push_back(new ApplyCode());
}

void ValNode::generate(ScopePrototype* scope)
{
	value->generate(scope);
	scope->body.push_back(new ValCode(index));
}

void BlockNode::generate(ScopePrototype* scope)
{
	
	for (Statements::const_iterator it = statements.begin(); it != statements.end(); ++it)
	{
		Node* statement = *it;
		statement->generate(scope);
		if (dynamic_cast<ExpressionNode*>(statement) != 0)
		{
			// if the statement is an expression, its result is ignored
			scope->body.push_back(new PopCode());
		}
	}
	value->generate(scope);
}

void DefNode::generate(ScopePrototype* scope)
{
	scope = this->scope;
	body->generate(scope);
	scope->body.push_back(new ReturnCode());
}

void ParentNode::generate(ScopePrototype* scope)
{
	scope->body.push_back(new ParentCode());
}

void TupleNode::generate(ScopePrototype* scope)
{
	for (Expressions::const_iterator it = expressions.begin(); it != expressions.end(); ++it)
	{
		Node* expression = *it;
		expression->generate(scope);
	}
	scope->body.push_back(new TupleCode(expressions.size()));
}

