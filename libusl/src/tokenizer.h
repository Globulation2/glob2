#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "token.h"
#include <vector>

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

#endif // ndef TOKENIZER_H
