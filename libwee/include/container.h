// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2004 Martin Voelkle <martin.voelkle@epfl.ch>

#ifndef CONTAINER_H
#define CONTAINER_H

#include "type.h"

class Container {
public:
	uint8_t* min;
	uint8_t* top;
	uint8_t* max;
public:
	Container(size_t pages);
	virtual ~Container() { }
	void Free();
	size_t Capacity() { return max - min; }
	bool Contains(void* ptr) { return ptr >= min && ptr < max; }
	virtual size_t Size() = 0;
	virtual uint8_t* Get(size_t offset = 0) = 0;
	virtual uint8_t* Alloc(size_t size) = 0;
private:
	void Init(size_t size);
protected:
	virtual bool Grow(size_t size) = 0;
};

class Heap: public Container {
public:
	Heap(size_t pages = 1): Container(pages) { top = min; }
	size_t Size() { return top - min; }
	uint8_t* Get(size_t offset = 0) {
		return min + offset;
	}
	uint8_t* Alloc(size_t size) {
		uint8_t* newTop = top + size;
		while(newTop > max) {
			if(!Grow(Capacity())) {
				return NULL;
			}
		}
		uint8_t* ret = top;
		top = newTop;
		return ret;
	}
	bool Grow(size_t size);
	Object* Push(const Object* object) {
		size_t size = sizeof(Object) + object->type->Size(&object->value);
		uint8_t* objectMem = (uint8_t*)object;
		uint8_t* newObjectMem = Alloc(size);
		std::copy(objectMem, objectMem + size, newObjectMem);
		return (Object*)newObjectMem;
	}
};

class Stack: public Container {
public:
	Stack(size_t pages = 1): Container(pages) { top = max; }
	size_t Size() { return max - top; }
	uint8_t* Get(size_t offset = 0) {
		return top + offset;
	}
	uint8_t* Alloc(size_t size) {
		uint8_t* newTop = top - size;
		while(newTop < min) {
			if(!Grow(Capacity())) {
				return NULL;
			}
		}
		top = newTop;
		return top;
	}
	uint8_t* Free(size_t size) {
		uint8_t* ret = top;
		top += size;
		return ret;
	}
	bool Grow(size_t size);
	template<typename T>
	T* Push(const T& src) {
		T* ret = (T*)Alloc(sizeof(T));
		*ret = src;
		return ret;
	}
	template<typename T>
	void Pop(T* dest) {
		*dest = *(const T*)Free(sizeof(T));
	}
};

#endif // ndef CONTAINER_H
