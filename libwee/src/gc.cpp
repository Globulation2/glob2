/*
  Copyright (C) 2004 Martin Voelkle <martin.voelkle@epfl.ch>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "gc.h"
#include "asm.h"

Object* CopyGC::New(const Type* type, size_t allocSize) {
	Object* object = (Object*)heap.Alloc(allocSize);
	if(object == NULL) {
		Collect();
		object = (Object*)heap.Alloc(allocSize);
		if(object == NULL) {
			//if(!Resize(newHeapEnd - heap.begin)) {
				abort();
			//}
		}
	}
	::new(static_cast<void*>(object)) Object(type);
	return object;
}

const Value* CopyGC::Scan(const Type* type, const Value* value, Heap& dest) {
	switch(type->meta) {
	case Type::Builtin:
		return Scan((const Types::Builtin*)type, (const Values::Builtin*)value, dest);
	case Type::Compound:
		return Scan((const Types::Compound*)type, (const Values::Compound*)value, dest);
	case Type::Array:
		return Scan((const Types::Array*)type, (const Values::Array*)value, dest);
	case Type::VarArray:
		return Scan((const Types::VarArray*)type, (const Values::VarArray*)value, dest);
	default:
		abort();
	}
}

const Value* CopyGC::Scan(const Types::Builtin* type, const Values::Builtin* value, Heap& dest) {
	if(type == Types::Builtin::ObjectRef) {
		Object*& objectRef = const_cast<Object*&>(((const Values::ObjectRef*)value)->value.object);
		Object* object = objectRef;
		if(heap.Contains(object)) {
			Object* newObject = *(Object**)(void*)object;
			if(dest.Contains(newObject)) {
				objectRef = newObject;
			}
			else {
				Object* newObject = dest.Push(object);
				*(Object**)(void*)object = newObject;
			}
		}
	}
	/*else if(type == Types::Builtin::ValueRef) {
		// TODO: scan value references
		abort();
	}*/
	return value + type->size;
}

const Value* CopyGC::Scan(const Types::Compound* type, const Values::Compound* value, Heap& dest) {
	const Value* iter = value;
	for(size_t i = 0; i < type->fieldsCount; ++i) {
		iter = Scan(type->fields[i].type, iter, dest);
	}
	return iter;
}

const Value* CopyGC::Scan(const Types::Array* type, const Values::Array* value, Heap& dest) {
	const Value* iter = value;
	for(size_t i = 0; i < type->elemsCount; ++i) {
		iter = Scan(type->elemsType, iter, dest);
	}
	return iter;
}

const Value* CopyGC::Scan(const Types::VarArray* type, const Values::VarArray* value, Heap& dest) {
	const Value* iter = value->elems;
	for(size_t i = 0; i < value->elemsCount; ++i) {
		iter = Scan(type->elemsType, iter, dest);
	}
	return iter;
}

void CopyGC::Collect() {
	Heap newHeap(heap.Capacity());
	// scan root
	ObjectRef root((Object*)heap.min, NULL);
	Values::ObjectRef rootValue(root);
	Scan(Types::Builtin::ObjectRef, &rootValue, newHeap);
	// scan heap
	for(const Object* iter = (const Object*)newHeap.min; iter < (const Object*)newHeap.top; iter = (const Object*)Scan(iter->type, &iter->value, newHeap));
	// replace heap
	heap.Free();
	std::swap(heap, newHeap);
}
