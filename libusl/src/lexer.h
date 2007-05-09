#ifndef LEXER_H
#define LEXER_H

#include "tokenizer.h"

class Lexer: Tokenizer
{
public:
	enum TokenType
	{
		ID,
		STR,
		NUM,
		LPAR,
		RPAR,
		LBRACE,
		RBRACE,
		COMMA,
		SPACE,
		COMMENT,
		END,
		ERROR,
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
	Token _next()
	{
		Token token = Tokenizer::next();
		while ((token.type->id == SPACE) || (token.type->id == COMMENT))
			token = Tokenizer::next();
		return token;
	}
	
public:
	Token token;
};

#endif // ndef LEXER_H
