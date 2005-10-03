#include "parser.h"

const Token::Type Parser::tokenTypes[TOKENTYPES] = {
	Token::Type(ID,      "an identifier",           "[[:alpha:]][[:alnum:]]*"),
	Token::Type(OP,      "an operator",             "[!#$%&*+-./:<=>?@\\^_\\|~]+"),
	Token::Type(STR,     "a string",                "([\"\'])(\\\\[\"\']|[^\"\'[:cntrl:]])*[\"\']"),
	Token::Type(NUM,     "a number",                "[[:digit:]]+|(0x|0X)[[:xdigit:]]+"),
	Token::Type(SBEG,    "a beginning of sequence", "\\(|\\[|\\{"),
	Token::Type(SEND,    "an end of sequence",      "\\)|\\]|\\}"),
	Token::Type(SSEP,    "a separator",             "[,;\\.]"),
	Token::Type(SPACE,   "a space",                 "[[:space:]]+"),
	Token::Type(COMMENT, "a comment",               "\\([\"\'])(\\\\|[^\\])*\\([\"\'])"),
	Token::Type(END,     "the end of the text",     "$"),
	Token::Type(ERROR,   "an invalid token",        ""),
};

Tree* Parser::Parse() {
	const Position position(next.position);
	std::vector<Tree*> root;
	while(next.type->id != END) {
		root.push_back(Next());
	}
	return new Trees::Sequence(position, root);
}

Tree* Parser::Next() {
	Tree* tree = Next2();
	while(next.type->id == ID
	   || next.type->id == OP
	   || next.type->id == STR
	   || next.type->id == NUM
	   || next.type->id == SBEG) {
		tree = new Trees::Apply(next.position, tree, Next2());
	}
	return tree;
}

Tree* Parser::Next2() {
	Tree* tree;
	switch(next.type->id) {
		case ID:
			tree = new Trees::Strings::Identifier(next.position, std::string(next.text, next.length));
			NextToken();
			break;
		case OP:
			tree = new Trees::Strings::Operator(next.position, std::string(next.text, next.length));
			NextToken();
			break;
		case STR: // TODO: parse strings correctly (remove escape chars)
			tree = new Trees::String(next.position, std::string(next.text, next.length));
			NextToken();
			break;
		case NUM: // TODO: parse numbers correctly and put the value in the tree
			tree = new Trees::Number(next.position, std::string(next.text, next.length));
			NextToken();
			break;
		case SBEG: {
			const char begin = *next.text;
			std::vector<Tree*> sequence;
			NextToken();
			if(next.type->id != SEND) {
				while(true) {
					sequence.push_back(Next());
					if(next.type->id == SSEP) {
						NextToken();
					}
					else {
						break;
					}
				}
				if(next.type->id != SEND) {
					Fail(std::string("unterminated sequence: found ") + next.type->desc + " while expecting " + tokenTypes[SEND].desc);
				}
			}
			const char found = *next.text;
			char expected;
			switch(begin) {
				case '(':
					expected = ')';
					break;
				case '[':
					expected = ']';
					break;
				case '{':
					expected = '}';
					break;
				default:
					Fail("parser bug: invalid beginning of sequence accepted");
			}
			if(found != expected) {
				Fail(std::string("wrong end of sequence: found ") + found + " while expecting " + expected);
			}
			tree = new Trees::Sequence(next.position, sequence);
			NextToken();
			break;
		}
		default:
			Fail(next.type->desc + " was found while expecting " + tokenTypes[ID].desc + ", " + tokenTypes[OP].desc + ", " + tokenTypes[STR].desc + ", " + tokenTypes[NUM].desc + " or " + tokenTypes[SBEG].desc);
	}
	return tree;
}
