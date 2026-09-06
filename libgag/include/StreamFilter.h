// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

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
