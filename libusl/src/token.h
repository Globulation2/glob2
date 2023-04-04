#ifndef TOKEN_H
#define TOKEN_H

#include "position.h"
#include <string>
#ifdef _MSC_VER
#include <BaseTsd.h>
using size_t = SIZE_T;
using ssize_t = SSIZE_T;
#else
#include <unistd.h>
#endif

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
