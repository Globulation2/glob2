// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __BITARRAY_H
#define __BITARRAY_H

#include <valarray>

namespace Utilities
{
	class BitArray
	{
	private:
		std::valarray<unsigned char> values;
		size_t bitLength;
		
		size_t bitToByte(size_t v) const;
		void assertPos(size_t pos) const;
		
	public:
		BitArray() { bitLength = 0; }
		BitArray(size_t size, bool defaultValue = false);
		void resize(size_t size, bool defaultValue = false);
		size_t getBitLength(void) const { return bitLength; }
		size_t getByteLength(void) const { return values.size(); }
		void set(size_t pos, bool value);
		bool get(size_t pos) const;
		void serialize(unsigned char *stream) const;
		void deserialize(const unsigned char *stream, size_t size);
	};
}

#endif
