// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2004 Martin Voelkle <martin.voelkle@epfl.ch>

#ifndef FUNCTION_H
#define FUNCTION_H

#include "container.h"

struct Function {
	virtual void call(Value* frame) = 0;
};

namespace Functions {
	
	struct Native {
		void call(Stack* stack) { code(stack); }
		void (*code)(Stack*);
	};
	
	struct Interpreted {
		void call(Stack* stack);
		uint8_t* code;
	};
	
};

#endif // ndef FUNCTION_H
