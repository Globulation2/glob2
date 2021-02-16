/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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
