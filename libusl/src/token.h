#ifndef TOKEN_H
#define TOKEN_H

#include "position.h"
#include <string>
#include <unistd.h>

struct Token
{
	class Type
	{
	public:
		Type(int id, const std::string& desc, const char* pattern);
		ssize_t match(const char* string) const;
	
		int id;
		std::string desc;
		
	private:
		//! private copy operator to forbid copies, won't link if used
		Type& operator=(const Type& type);
		void init(const char* pattern, size_t length);
	
		struct Regexp;
		Regexp* regexp;
	};
	
	
	Token(const Position& position, const Type* type, const char* text, size_t length);
	std::string string() const { return std::string(text, length); }
	
	Position position;
	const Type* type;
	const char* text;
	size_t length;
};

#endif // ndef TOKEN_H
