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

#ifndef __STREAMFILTER_H
#define __STREAMFILTER_H

#include "StreamBackend.h"

namespace GAGCore
{
	// WARNING, does not work as it, please fix
	// TODO : FIX
	// FIXME : TODO

	//! Uncompress from a StreamBackend
	class CompressedInputStreamBackendFilter : public MemoryStreamBackend
	{
	public:
		//! Use backend as the source
		CompressedInputStreamBackendFilter(StreamBackend *backen);
	};

	//! Uncompress from a StreamBackend
	class CompressedOutputStreamBackendFilter : public MemoryStreamBackend
	{
	protected:
		StreamBackend *backend;

	public:
		//! Use backend as the destination
		CompressedOutputStreamBackendFilter(StreamBackend *backen);
		//! Delete also the associated backend
		virtual ~CompressedOutputStreamBackendFilter();
		//! We are writing in memory, never out of stream
		virtual bool isEndOfStream() { return false; }
	};
}

#endif
