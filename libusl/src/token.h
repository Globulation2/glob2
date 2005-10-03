#ifndef TOKEN_H
#define TOKEN_H

#include "position.h"
#include <string>
#include <cassert>
#include <regex.h>

struct Token {
	struct Type {
		int id;
		std::string desc;
		Type(int id, const std::string& desc, const char* pattern): id(id), desc(desc) {
			Init(pattern, std::char_traits<char>::length(pattern));
		}
		Type(int id, const std::string& desc, const std::string& pattern): id(id), desc(desc) {
			Init(pattern.data(), pattern.size());
		}
		Type(const Type& type) {
			self = type;
		}
		Type& operator=(const Type& type) {
			id = type.id;
			desc = type.desc;
			regexp = type.regexp;
			regexp->Ref();
			return self;
		}
		~Type() { regexp->UnRef(); }
		ssize_t Match(const char* string) const {
			regmatch_t match;
			int result = regexec(&regexp->regex, string, 1, &match, 0);
			if(result == 0) {
				assert(match.rm_so == 0);
				return match.rm_eo;
			}
			else {
				return -1;
			}
		}
	private:
		void Init(const char* pattern, size_t length) {
			std::string newPattern;
			newPattern.reserve(2 + length + 2);
			newPattern.push_back('^'); // match at the beginning
			newPattern.push_back('(');
			newPattern.append(pattern, length);
			newPattern.push_back(')');
			newPattern.push_back('\0');
			regexp = new Regexp(newPattern.data(), REG_EXTENDED);
		}
		struct Regexp {
			size_t refCount;
			regex_t regex;
			Regexp(const char* pattern, int cflags = 0): refCount(0) { regcomp(&regex, pattern, cflags); }
			~Regexp() { regfree(&regex); }
			void Ref() { ++refCount;}
			void UnRef() { if(refCount == 0) delete this; else --refCount; }
		};
		Regexp* regexp;
	};
	Position position;
	const Type* type;
	const char* text;
	size_t length;
	Token(const Position& position, const Type* type, const char* text, size_t length): position(position), type(type), text(text), length(length) {}
};

#endif // ndef TOKEN_H
