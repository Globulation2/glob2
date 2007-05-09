#include "lexer.h"

const Token::Type Lexer::tokenTypes[] =
{
	Token::Type(SPACE,   "a space",                 "[[:blank:]]+"),
	Token::Type(VAL,     "'val'",                   "val"),
	Token::Type(ASSIGN,  "'='",                     "="),
	Token::Type(ID,      "an identifier",           "([[:alpha:]][[:alnum:]]*)|([!#$%&*+-./:<=>?@\\^_\\|~]+)"),
	Token::Type(STR,     "a string",                "([\"\'])(\\\\[\"\']|[^\"\'[:cntrl:]])*[\"\']"),
	Token::Type(NUM,     "a number",                "[[:digit:]]+|(0x|0X)[[:xdigit:]]+"),
	Token::Type(LPAR,    "a beginning of sequence", "\\("),
	Token::Type(RPAR,    "an end of sequence",      "\\)"),
	Token::Type(LBRACE,  "a beginning of sequence", "\\{"),
	Token::Type(RBRACE,  "an end of sequence",      "\\}"),
	Token::Type(COMMA,   "a comma",                 ","),
	Token::Type(COMMENT, "a comment",               "\\([\"\'])(\\\\|[^\\])*\\([\"\'])"),
	Token::Type(CR,      "a new line",              "\n"),
	Token::Type(END,     "the end of the text",     "$"),
	Token::Type(ERROR,   "an invalid token",        ""),
};
