#include "interpreter.h"
#include <typeinfo>

struct Function {
	virtual ~Function() {}
	virtual const Function* Call(const Function* arg) = 0;
};

struct Method: Function {
	Method(const Function* target): target(target) {}
	const Function* target;
};

struct Number: Function {
	Number(double value): value(value) {}
	const double value;
	const Function* Call(const Function* arg) {
		const std::type_info& argType(typeid(*arg));
		if(argType == typeid(Number)) {
			;
		}
		return NULL;
	}
};

struct String: Function {
	String(const std::string& value): value(value) {}
	const std::string value;
	const Function* Call(const Function* arg) {
		const std::type_info& argType(typeid(*arg));
		if(argType == typeid(Number)) {
			;
		}
		return NULL;
	}
};

struct Identifier: Function {
	Identifier(const std::string& name): name(name) {}
	const std::string name;
	const Function* Call(const Function* arg) {
		return NULL;
	}
};

const std::string Interpreter::Run(Context* context) {
}
