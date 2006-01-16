#include "scanner.h"

Token Scanner::Next() {
	const Token::Type* type = NULL;
	ssize_t length = -1;
	foreach(tokenTypeIterator, tokenTypes.begin(), tokenTypes.end()) {
		const Token::Type& newType = *tokenTypeIterator;
		ssize_t newLength = newType.Match(text);
		if(newLength > length) {
			type = &newType;
			length = newLength;
		}
	}
	if(length == -1) {
		length = 0;
	}
	Token token(position, type, text, length);
	position.Move(text, text + length);
	text += length;
	return token;
}
