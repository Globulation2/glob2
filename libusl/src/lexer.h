#pragma once

#include "token.h"

class Tokenizer
{
public:
	Tokenizer(const Token::Type *tokenTypes, const size_t tokenTypesSize, const std::string& filename, const char* text);
	const Token next();

private:
	const Token::Type *tokenTypes;
	const size_t tokenTypesSize;
	const char* text;
	Position position;
};

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

	Lexer(const std::string& filename, const char* text):
		Tokenizer(tokenTypes, TOKENTYPES, filename, text),
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
	
protected:
	[[noreturn]] void fail(const std::string& expected) const;
	
private:
	Token _next();
	
public:
	Token token;
};

