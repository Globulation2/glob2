#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "parser.h"
#include <map>

struct Function;

class Context: public std::map<const std::string, const Function*> {
	typedef std::map<const std::string, const Function*> super;
public:
	Context(Context* parent = NULL): parent(parent) {}
	iterator find(const key_type& key) {
		iterator it = super::find(key);
		if(it != end()) {
			return it;
		}
		else {
			it = parent->find(key);
			if(it != parent->end()) {
				return it;
			}
			else {
				return end();
			}
		}
	}
	const_iterator find(const key_type& key) const {
		const_iterator it = super::find(key);
		if(it != end()) {
			return it;
		}
		else {
			it = parent->find(key);
			if(it != parent->end()) {
				return it;
			}
			else {
				return end();
			}
		}
	}
	const Function*& operator[](const std::string& key) {
		Context* context = this;
		while(context != NULL) {
			iterator it = context->find(key);
			if(it != context->end()) {
				return it->second;
			}
		}
		return super::operator[](key);
	}
private:
	Context* parent;
};

class Interpreter: Parser {
public:
	Interpreter(const char* text): Parser(text) {}
	const std::string Run(Context* context);
private:
	const Function* Next();
};

#endif // ndef INTERPRETER_H
