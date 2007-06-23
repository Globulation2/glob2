#include "tokenizer.h"
#include <cassert>

Tokenizer::Tokenizer(const Token::Type *tokenTypes, const size_t tokenTypesSize, const char* text):
	tokenTypes(tokenTypes),
	tokenTypesSize(tokenTypesSize),
	text(text),
	position(1, 1)
{ }

const Token Tokenizer::next()
{
	const Token::Type* type = NULL;
	ssize_t length = -1;
	for (size_t i = 0; i < tokenTypesSize; i++)
	{
		const Token::Type& newType = tokenTypes[i];
		ssize_t newLength = newType.match(text);
		if(newLength > length)
		{
			type = &newType;
			length = newLength;
		}
	}
	if (length == -1)
		assert(false);
	Token token(position, type, text, length);
	position.move(text, length);
	text += length;
	return token;
}
