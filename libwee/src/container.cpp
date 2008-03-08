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
