#ifndef LEXER_H
#define LEXER_H

#include "tokenizer.h"

class Lexer: Tokenizer
{
public:
	enum TokenType
	{
		SPACE,
		VAL,
		ASSIGN,
		ID,
		STR,
		NUM,
		LPAR,
		RPAR,
		LBRACE,
		RBRACE,
		COMMA,
		COMMENT,
		NL,
		END,
		TOKENTYPES,
	};
	
private:
	static const Token::Type tokenTypes[TOKENTYPES];
	
public:
	Lexer(const char* text):
		Tokenizer(tokenTypes, TOKENTYPES, text),
		token(_next())
	{ }
	
	const Token& next()
	{
		token = _next();
		return token;
	}
	
	TokenType tokenType() const
	{
		return (TokenType) token.type->id;
	}
	
private:
	Token _next();
	
public:
	Token token;
};

#endif // ndef LEXER_H
