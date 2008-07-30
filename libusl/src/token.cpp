#include "token.h"
#include <cassert>
#include <regex.h>
#include <alloca.h>

struct Token::Type::Regexp
{
	regex_t regex;
	Regexp(const char* pattern, int cflags = 0);
	~Regexp() { regfree(&regex); }
};

Token::Type::Regexp::Regexp(const char* pattern, int cflags)
{
#ifdef NDEBUG
	regcomp(&regex, pattern, cflags);
#else
	int result = regcomp(&regex, pattern, cflags);
	if (result != 0)
	{
		size_t length = regerror(result, &regex, 0, 0);
		char* error = static_cast<char*>(alloca(length));
		regerror(result, &regex, error, length);
		fprintf(stderr, "regcomp error in '%s': %s\n", pattern, error);
		assert(false);
	}
#endif
}


Token::Type::Type(int id, const std::string& desc, const char* pattern): id(id), desc(desc)
{
	init(pattern, std::char_traits<char>::length(pattern));
}

ssize_t Token::Type::match(const char* string) const
{
	regmatch_t match;
	int result = regexec(&regexp->regex, string, 1, &match, 0);
	if(result == 0)
	{
		assert(match.rm_so == 0);
		return match.rm_eo;
	}
	else
	{
		return -1;
	}
}

void Token::Type::init(const char* pattern, size_t length)
{
	std::string newPattern;
	newPattern.reserve(2 + length + 2);
	newPattern.push_back('^'); // match at the beginning
	newPattern.push_back('(');
	newPattern.append(pattern, length);
	newPattern.push_back(')');
	newPattern.push_back('\0');
	regexp = new Regexp(newPattern.data(), REG_EXTENDED);
}

Token::Token(const Position& position, const Type* type, const char* text, size_t length):
	position(position), type(type), text(text), length(length)
{ }
