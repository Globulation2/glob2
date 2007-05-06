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
		token(Tokenizer::next())
	{
		eatSpaces();
	}
	
	const Token& next()
	{
		token = Tokenizer::next();
		eatSpaces();
		return token;
	}
	
	const Token& peek()
	{
		return token;
	}
	
	TokenType peekType()
	{
		return (TokenType) peek().type->id;
	}
	
private:
	void eatSpaces()
	{
		while ((token.type->id == SPACE) || (token.type->id == COMMENT))
			token = Tokenizer::next();
	}
	
	Token token;
};

#endif // ndef LEXER_H
