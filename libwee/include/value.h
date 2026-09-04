// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2004 Martin Voelkle <martin.voelkle@epfl.ch>

#ifndef VALUE_H
#define VALUE_H

#include "forward.h"

// builtin representations
typedef bool             Bool;
typedef uint_fast32_t    Char;
typedef void*            Ptr;
typedef unsigned long    Nat;
typedef long             Int;
typedef uint_fast8_t     Nat8;
typedef int_fast8_t      Int8;
typedef uint_fast16_t    Nat16;
typedef int_fast16_t     Int16;
typedef uint_fast32_t    Nat32;
typedef int_fast32_t     Int32;
typedef uint_fast64_t    Nat64;
typedef int_fast64_t     Int64;
struct                   ValueRef {
	const Object* object;
	const Value* value;
	ValueRef(const Object* object, const Value* value): object(object), value(value) {}
};
struct                   ObjectRef {
	const Object* object;
	const VTable vtable;
	ObjectRef(const Object* object, const VTable vtable): object(object), vtable(vtable) {}
};

struct Value {
};

namespace Values {
	
	// Builtin values
	struct Builtin: Value {
	};
	struct Void: Builtin {};
	#define VALUES_BUILTIN_VALUE(T) struct T: Builtin { const ::T value; T(const ::T& value): value(value) {} }
	VALUES_BUILTIN_VALUE(Bool);
	VALUES_BUILTIN_VALUE(Char);
	VALUES_BUILTIN_VALUE(Nat);
	VALUES_BUILTIN_VALUE(Int);
	VALUES_BUILTIN_VALUE(Nat8);
	VALUES_BUILTIN_VALUE(Int8);
	VALUES_BUILTIN_VALUE(Nat16);
	VALUES_BUILTIN_VALUE(Int16);
	VALUES_BUILTIN_VALUE(Nat32);
	VALUES_BUILTIN_VALUE(Int32);
	VALUES_BUILTIN_VALUE(Nat64);
	VALUES_BUILTIN_VALUE(Int64);
	VALUES_BUILTIN_VALUE(ValueRef);
	VALUES_BUILTIN_VALUE(ObjectRef);
	
	struct Compound: Value {
	};
	
	struct Array: Value {
		Value elems[0];
	};
	
	struct VarArray: Value {
		const size_t elemsCount;
		Value elems[0];
		VarArray(size_t elemsCount): elemsCount(elemsCount) {}
	};
	
};

struct Object {
	const Type* type;
	Value value;
	Object(const Type* type): type(type) {}
};

#endif // ndef VALUE_H
