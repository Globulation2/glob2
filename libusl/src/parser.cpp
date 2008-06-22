#include "parser.h"
#include "types.h"
#include "error.h"

using namespace std;

BlockNode* Parser::parse()
{
	auto_ptr<ExecutionBlock> block(new ExecutionBlock(Position()));
	statements(block.get());
	return block.release();
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
		{
			string name = identifier();
			switch (tokenType())
			{
			case COLON:
			case COLONEQ:
				return declaration(position, DecNode::AUTO, name);
			default:
				return expression(position, name);
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
	Node* expr = expression();
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

Node* Parser::expression()
{
	return expression(simple());
}

Node* Parser::expression(const Position& position, const string& id)
{
	return expression(new DefLookupNode(position, id));
}

Node* Parser::expression(Node* first)
{
	auto_ptr<Node> node(first);
	while (true)
	{
		switch (tokenType())
		{
		case ID:
		case PREFIX:
			node.reset(selectAndApply(token.position, node, identifier()));
			break;
		case DOT:
		{
			next();
			const string& selected = identifier();
			node.reset(new SelectNode(token.position, node.release(), selected));
			break;
		}
		case LPAR:
		case LBRACE:
		case LBRACK:
			node.reset(selectAndApply(token.position, node, "apply"));
			break;
		default:
			return node.release();
		}
	}
}

ApplyNode* Parser::selectAndApply(const Position& position, auto_ptr<Node> receiver, const string& method)
{
	Node* argument = simple();
	return new ApplyNode(position, new SelectNode(position, receiver.release(), method), argument);
}

Node* Parser::simple()
{
	Position position = token.position;
	switch (tokenType())
	{
	case PREFIX:
		{
			const string& op = identifier();
			return new SelectNode(token.position, simple(), op);
		}
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
	case LPAR:
		{
			next();
			auto_ptr<RecordBlock> block(new RecordBlock(position));
			statements(block.get());
			accept(RPAR);
			RecordBlock* record = block.get();
			switch(record->elements.size()) {
			case 0:
				return new ConstNode(position, &nil);
			case 1:
				{
					Node* element = dynamic_cast<Node*>(record->elements.front());
					if (element != 0)
					{
						record->elements.clear();
						return element;
					}
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
			Node* body = expression();
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

