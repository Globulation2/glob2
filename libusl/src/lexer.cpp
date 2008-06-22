#include "lexer.h"

#include <cassert>

const Token::Type Lexer::tokenTypes[] =
{
	Token::Type(SPACE,   "a space",                 "[[:blank:]]+"),
	Token::Type(VAL,     "'val'",                   "val"),
	Token::Type(DEF,     "'def'",                   "def"),
	Token::Type(FUN,     "'fun'",                   "fun"),
	Token::Type(ARROW,   "'=>'",                    "=>"),
	Token::Type(EQUALS,  "'='",                     "="),
	Token::Type(COLON,   "':'",                     ":"),
	Token::Type(COLONEQ, "':='",                    ":="),
	Token::Type(WILDCARD,"'_'",                     "_"),
	Token::Type(DOT,     "a dot",                   "\\."),
	Token::Type(COMMA,   "a comma",                 ","),
	Token::Type(PREFIX,  "a prefix operator",       "!"),
	Token::Type(ID,      "an identifier",           "([[:alpha:]][[:alnum:]]*)|([!#$%&*+/:<=>?@^_|~-]+)"),
	Token::Type(STR,     "a string",                "([\"\'])(\\\\[\"\']|[^\"\'[:cntrl:]])*[\"\']"),
	Token::Type(NUM,     "a number",                "[[:digit:]]+|(0x|0X)[[:xdigit:]]+"),
	Token::Type(LPAR,    "a beginning of tuple",    "\\("),
	Token::Type(RPAR,    "an end of tuple",         "\\)"),
	Token::Type(LBRACE,  "a beginning of block",    "\\{"),
	Token::Type(RBRACE,  "an end of block",         "\\}"),
	Token::Type(LBRACK,  "a beginning of array",    "\\["),
	Token::Type(RBRACK,  "an end of array",         "\\]"),
	Token::Type(COMMENT, "a comment",               "(\\\\\\*([^\\\\]|\\\\[^\\*])*\\\\\\*)|(\\\\#[^\n]*\n)"),
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
