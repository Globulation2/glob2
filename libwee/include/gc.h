// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2004 Martin Voelkle <martin.voelkle@epfl.ch>

#ifndef GC_H
#define GC_H

#include "container.h"
#include "type.h"
//#include "asm.h"

class GC {
public:
	virtual ~GC() { }
	virtual Object* New(const Type* type, size_t allocSize) = 0;
	virtual void Collect() = 0;
};

class CopyGC {
	Heap heap;
public:
	CopyGC(): heap() {}
	CopyGC(size_t heapPages): heap(heapPages) {}
public:
	Object* New(const Type* type, size_t allocSize);
	void Collect();
private:
	//void Resize(size_t newSize);
	const Value* Scan(const Type* type, const Value* value, Heap& dest);
	const Value* Scan(const Types::Builtin* type, const Values::Builtin* value, Heap& dest);
	const Value* Scan(const Types::Compound* type, const Values::Compound* value, Heap& dest);
	const Value* Scan(const Types::Array* type, const Values::Array* value, Heap& dest);
	const Value* Scan(const Types::VarArray* type, const Values::VarArray* value, Heap& dest);
};

#endif // ndef GC_H
