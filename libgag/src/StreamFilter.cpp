/*
  Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charrière
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

#include <StreamFilter.h>
#include <BinaryStream.h>
#include <valarray>
#include "zlib.h"
#include <iostream>

namespace GAGCore
{
	CompressedInputStreamBackendFilter::CompressedInputStreamBackendFilter(StreamBackend *backend)
	{
		assert(backend);
		// read
		BinaryInputStream *stream = new BinaryInputStream(backend);
		Uint32 compressedLength = stream->readUint32("compressedLength");
		Uint32 uncompressedLength = stream->readUint32("uncompressedLength");
		
		std::valarray<unsigned char> source(compressedLength);
		std::valarray<unsigned char> dest(uncompressedLength);
		unsigned long destLength;
		std::cout << "Decompressing " << compressedLength <<  " bytes to " << uncompressedLength << " bytes." << std::endl;
		
		stream->read(&source[0], compressedLength, "compressedDatas");
		delete stream;
		
		// decompress
		uncompress(&dest[0], &destLength, &source[0], compressedLength);
		assert(destLength == uncompressedLength);
		
		this->write(&dest[0], uncompressedLength);
		this->seekFromStart(0);
	}
	
	CompressedOutputStreamBackendFilter::CompressedOutputStreamBackendFilter(StreamBackend *backend)
	{
		assert(backend);
		this->backend = backend;
	}
	
	CompressedOutputStreamBackendFilter::~CompressedOutputStreamBackendFilter()
	{
		// compress
		this->seekFromEnd(0);
		Uint32 uncompressedLength = static_cast<Uint32>(this->getPosition());
		Uint32 compressedLength = (uncompressedLength << 1) + 12; // dest should be at least 0.1% + 12 bytes source length, let's take some margin... in paging we trust
		this->seekFromStart(0);
		
		std::valarray<unsigned char> source(uncompressedLength);
		std::valarray<unsigned char> dest(compressedLength);
		
		this->read(&source[0], uncompressedLength);
		compress(&dest[0], (uLongf *)&compressedLength, &source[0], uncompressedLength);
		std::cout << "Compressing " << uncompressedLength <<  " bytes into " << compressedLength << " bytes." << std::endl;
		
		BinaryOutputStream *stream = new BinaryOutputStream(backend);
		stream->writeUint32(compressedLength, "compressedLength");
		stream->writeUint32(uncompressedLength, "uncompressedLength");
		stream->write(&dest[0], compressedLength, "compressedDatas");
		delete stream;
	}
}
