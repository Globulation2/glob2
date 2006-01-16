#include "parser.h"

const Token::Type Parser::tokenTypes[TOKENTYPES] = {
	Token::Type(ID,      "an identifier",           "[[:alpha:]][[:alnum:]]*"),
	Token::Type(OP,      "an operator",             "[!#$%&*+-./:<=>?@\\^_\\|~]+"),
	Token::Type(STR,     "a string",                "([\"\'])(\\\\[\"\']|[^\"\'[:cntrl:]])*[\"\']"),
	Token::Type(NUM,     "a number",                "[[:digit:]]+|(0x|0X)[[:xdigit:]]+"),
	Token::Type(SBEG,    "a beginning of sequence", "\\(|\\[|\\{"),
	Token::Type(SEND,    "an end of sequence",      "\\)|\\]|\\}"),
	Token::Type(SSEP,    "a sequence separator",    "[,;]"),
	Token::Type(SPACE,   "a space",                 "[[:space:]]+"),
	Token::Type(COMMENT, "a comment",               "\\([\"\'])(\\\\|[^\\])*\\([\"\'])"),
	Token::Type(END,     "the end of the text",     "$"),
	Token::Type(ERROR,   "an invalid token",        ""),
};
