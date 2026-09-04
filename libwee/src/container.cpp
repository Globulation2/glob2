// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2004 Martin Voelkle <martin.voelkle@epfl.ch>

#include "container.h"
#include <sys/mman.h>

#define PAGE_SIZE sysconf(_SC_PAGESIZE)
#define DEFAULT_STACK_SIZE (PAGE_SIZE*8)

Container::Container(size_t pages) {
	Init(pages * PAGE_SIZE);
}

void Container::Free() {
	munmap(min, Capacity());
}

void Container::Init(size_t size) {
	min = (uint8_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 0, 0);
	max = min + size;
}

bool Heap::Grow(size_t size) {
	void* result = mmap(max, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 0, 0);
	if(result != (void*)-1) {
		max += size;
		return true;
	}
	else {
		return false;
	}
}

bool Stack::Grow(size_t size) {
	void* result = mmap(min - size, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 0, 0);
	if(result != (void*)-1) {
		min -= size;
		return true;
	}
	else {
		return false;
	}
}
