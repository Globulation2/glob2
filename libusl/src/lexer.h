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
		DEF,
		FUN,
		ARROW,
		EQUALS,
		COLON,
		COLONEQ,
		WILDCARD,
		DOT,
		ID,
		STR,
		NUM,
		LPAR,
		RPAR,
		LBRACE,
		RBRACE,
		LBRACK,
		RBRACK,
		COMMA,
		PREFIX,
		COMMENT,
		NL,
		END,
		TOKENTYPES,
	};
	
private:
	static const Token::Type tokenTypes[TOKENTYPES];
	
public:
	static const Token::Type* getType(TokenType id)
	{
		for (size_t i = 0; i < TOKENTYPES; ++i)
			if (tokenTypes[i].id == id)
				return &tokenTypes[i];
		return 0;
	}

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
