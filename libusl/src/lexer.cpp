#include "lexer.h"

#include <cassert>

const Token::Type Lexer::tokenTypes[] =
{
	Token::Type(SPACE,   "a space",                 "[[:blank:]]+"),
	Token::Type(VAL,     "'val'",                   "val"),
	Token::Type(DEF,     "'def'",                   "def"),
	Token::Type(ASSIGN,  "'='",                     "="),
	Token::Type(COLON,   "':'",                     ":"),
	Token::Type(WILDCARD,"'_'",                     "_"),
	Token::Type(DOT,     "a dot",                   "\\."),
	Token::Type(COMMA,   "a comma",                 ","),
	Token::Type(ID,      "an identifier",           "([[:alpha:]][[:alnum:]]*)|([!#$%&*+/:<=>?@^_|~-]+)"),
	Token::Type(STR,     "a string",                "([\"\'])(\\\\[\"\']|[^\"\'[:cntrl:]])*[\"\']"),
	Token::Type(NUM,     "a number",                "[[:digit:]]+|(0x|0X)[[:xdigit:]]+"),
	Token::Type(LPAR,    "a beginning of sequence", "\\("),
	Token::Type(RPAR,    "an end of sequence",      "\\)"),
	Token::Type(LBRACE,  "a beginning of sequence", "\\{"),
	Token::Type(RBRACE,  "an end of sequence",      "\\}"),
	Token::Type(COMMENT, "a comment",               "\\([\"\'])(\\\\|[^\\])*\\([\"\'])"),
	Token::Type(NL,      "a new line",              "\n"),
	Token::Type(END,     "the end of the text",     "$"),
};

Token Lexer::_next()
{
	Token token = Tokenizer::next();
	while ((token.type->id == SPACE) || (token.type->id == COMMENT))
		token = Tokenizer::next();
	if (token.type == 0)
	{
		assert(false); // TODO
	}
	return token;
}
