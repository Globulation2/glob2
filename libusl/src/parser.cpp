#include "parser.h"
#include "types.h"
#include "error.h"
#include <memory>

using namespace std;

void Parser::parse(BlockNode* block)
{
	statements(block);
}

void Parser::statements(BlockNode* block)
{
	newlines();
	while (true)
	{
		switch (tokenType())
		{
		case END:
		case RPAR:
		case RBRACE:
		case RBRACK:
			return;
		case COMMA:
			next();
			break;
		default:
			block->elements.push_back(statement());
			newlines();
		}
	}
}

Node* Parser::statement()
{
	Position position = token.position;
	switch (tokenType())
	{
	case VAL:
		return declaration(DecNode::VAL);
	case DEF:
		return declaration(DecNode::DEF);
	case ID:
	case PREFIX:
		{
			bool isPrefix = tokenType() == PREFIX;
			string name = identifier();
			switch (tokenType())
			{
			case COLON:
			case COLONEQ:
				return declaration(position, DecNode::AUTO, name);
			default:
				if (!isPrefix)
					return methodCompositionExpression(pathExpression(new DefLookupNode(position, name)));
				else
					return new SelectNode(position, expression(), name);
			}
		}
	default:
		return expression();
	}
}

DecNode* Parser::declaration(DecNode::Type type) {
	next();
	return declaration(token.position, type, identifier());
}

DecNode* Parser::declaration(const Position& position, DecNode::Type type, const std::string& name) {
	auto_ptr<PatternNode> arg;
	switch (tokenType()) {
	case COLON:
	case COLONEQ:
		break;
	default:
		arg.reset(pattern());
	}
	switch (tokenType()) {
	case COLON:
		next();
		// TODO: type
		accept(EQUALS);
		break;
	case COLONEQ:
		next();
		break;
	default:
		fail(getType(COLON)->desc);
	}
	newlines();
	ExpressionNode* expr = expression();
	if (arg.get())
		expr = new FunNode(position, arg.release(), expr);
	return new DecNode(position, type, name, expr);
}

PatternNode* Parser::pattern()
{
	Position position = token.position;
	switch (tokenType())
	{
	case LPAR:
		next();
		if (tokenType() == RPAR)
		{
			return new NilPatternNode(position);
		}
		else
		{
			auto_ptr<TuplePatternNode> tuple(new TuplePatternNode(position));
			tuple->members.push_back(pattern());
			while (tokenType() == COMMA)
			{
				next();
				tuple->members.push_back(pattern());
			}
			accept(RPAR);
			if (tuple->members.size() > 1)
				return tuple.release();
			else
			{
				PatternNode* pattern = tuple->members.back();
				tuple->members.pop_back();
				return pattern;
			}
		}
	case ID:
		{
			string name = identifier();
			if (tokenType() == COLON)
			{
				next();
				expression(); // TODO: do something with the type
			}
			return new ValPatternNode(position, name);
		}
	case DEF:
		{
			next();
			string name = identifier();
			return new DefPatternNode(position, name);
		}
	case WILDCARD:
		next();
		return new IgnorePatternNode(position);
	default:
		fail("a pattern");
	}
}

void Parser::expressions(BlockNode* block)
{
	newlines();
	while (true)
	{
		switch (tokenType())
		{
		case END:
		case RPAR:
		case RBRACE:
		case RBRACK:
			return;
		case COMMA:
			next();
			break;
		default:
			block->elements.push_back(expression());
			newlines();
		}
	}
}

ExpressionNode* Parser::expression()
{
	return prefixedExpression();
}

ExpressionNode* Parser::prefixedExpression()
{
	const Position position(token.position);
	switch(tokenType())
	{
	case PREFIX:
	{
		const string name(identifier());
		return new SelectNode(position, expression(), name);
	}
	default:
		return methodCompositionExpression(pathExpression(simpleExpression()));
	}
}

ExpressionNode* Parser::methodCompositionExpression(ExpressionNode* first)
{
	auto_ptr<ExpressionNode> node(pathExpression(first));
	while (true)
	{
		const Position position(token.position);
		switch (tokenType())
		{
		case ID:
		case PREFIX:
		{
			string method = identifier();
			ExpressionNode* argument = pathExpression(simpleExpression());
			node.reset(new ApplyNode(position, new SelectNode(position, node.release(), method), argument));
			break;
		}
		default:
			return node.release();
		}
	}
}

ExpressionNode* Parser::pathExpression(ExpressionNode* first)
{
	auto_ptr<ExpressionNode> node(first);
	while (true)
	{
		const Position& position = token.position;
		switch (tokenType())
		{
		case DOT:
		{
			next();
			const string& selected = identifier();
			node.reset(new SelectNode(position, node.release(), selected));
			break;
		}
		case LPAR:
		case LBRACE:
		case LBRACK:
		{
			ExpressionNode* argument = simpleExpression();
			node.reset(new ApplyNode(position, node.release(), argument));
			break;
		}
		default:
			return node.release();
		}
	}
}

ExpressionNode* Parser::simpleExpression()
{
	const Position position(token.position);
	switch (tokenType())
	{
	case PREFIX:
	case ID:
		{
			return new DefLookupNode(position, identifier());
		}
	case NUM:
		{
			string str = token.string();
			next();
			int value = atoi(str.c_str());
			return new ConstNode(position, new Integer(heap, value));
		}
	case STR:
		{
			// TODO: implements strings
			assert(false);
		}
	case LPAR:
		{
			next();
			auto_ptr<RecordBlock> block(new RecordBlock(position));
			expressions(block.get());
			accept(RPAR);
			RecordBlock* record = block.get();
			switch(record->elements.size()) {
			case 0:
				return new ConstNode(position, &nil);
			case 1:
				{
					ExpressionNode* element = dynamic_cast<ExpressionNode*>(record->elements.front());
					assert(element);
					record->elements.clear();
					return element;
				}
			}
			return block.release();
		}
	case LBRACE:
		{
			next();
			auto_ptr<ExecutionBlock> block(new ExecutionBlock(position));
			statements(block.get());
			accept(RBRACE);
			return block.release();
		}
	case LBRACK:
		{
			next();
			auto_ptr<RecordBlock> block(new RecordBlock(position));
			statements(block.get());
			accept(RBRACK);
			return block.release();
		}
	case FUN:
		{
			next();
			auto_ptr<PatternNode> arg(pattern());
			accept(ARROW);
			ExpressionNode* body = expression();
			return new FunNode(position, arg.release(), body);
		}
	default:
		fail("an expression");
	}
}

void Parser::newlines()
{
	while (tokenType() == NL)
	{
		next();
	}
}

string Parser::identifier()
{
	switch (tokenType())
	{
	case ID:
	case PREFIX:
	{
		string id = token.string();
		next();
		return id;
	}
	default:
		fail(getType(ID)->desc);
	}
}

void Parser::accept(TokenType type)
{
	if (tokenType() == type)
	{
		next();
	}
	else
	{
		fail(getType(type)->desc);
	}
}

