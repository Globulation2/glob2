// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <SDL.h>
#include <SDL_endian.h>
#include <string.h>

// Useful function for marshalling
// Wire fields may start at any byte offset; memcpy avoids alignment and aliasing UB.

// 32 bit:

inline void addSint32(const Uint8 *data, Sint32 val, int pos)
{
	const Uint32 wire=SDL_SwapBE32(static_cast<Uint32>(val));
	memcpy(const_cast<Uint8 *>(data)+pos, &wire, sizeof(wire));
}

inline Sint32 getSint32(const Uint8 *data, int pos)
{
	Uint32 wire;
	memcpy(&wire, data+pos, sizeof(wire));
	return static_cast<Sint32>(SDL_SwapBE32(wire));
}

inline void addUint32(const Uint8 *data, Uint32 val, int pos)
{
	const Uint32 wire=SDL_SwapBE32(static_cast<Uint32>(val));
	memcpy(const_cast<Uint8 *>(data)+pos, &wire, sizeof(wire));
}

inline Uint32 getUint32(const Uint8 *data, int pos)
{
	Uint32 wire;
	memcpy(&wire, data+pos, sizeof(wire));
	return static_cast<Uint32>(SDL_SwapBE32(wire));
}

inline Uint32 getUint32RAW(const Uint8 *data, int pos)
{
	Uint32 value;
	memcpy(&value, data+pos, sizeof(value));
	return value;
}

// 16 bit:

inline void addSint16(const Uint8 *data, Sint16 val, int pos)
{
	const Uint16 wire=SDL_SwapBE16(static_cast<Uint16>(val));
	memcpy(const_cast<Uint8 *>(data)+pos, &wire, sizeof(wire));
}

inline void addUint16(const Uint8 *data, Uint16 val, int pos)
{
	const Uint16 wire=SDL_SwapBE16(static_cast<Uint16>(val));
	memcpy(const_cast<Uint8 *>(data)+pos, &wire, sizeof(wire));
}

inline Sint16 getSint16(const Uint8 *data, int pos)
{
	Uint16 wire;
	memcpy(&wire, data+pos, sizeof(wire));
	return static_cast<Sint16>(SDL_SwapBE16(wire));
}

inline Uint16 getUint16(const Uint8 *data, int pos)
{
	Uint16 wire;
	memcpy(&wire, data+pos, sizeof(wire));
	return static_cast<Uint16>(SDL_SwapBE16(wire));
}

// 8 bit:

inline void addUint8(const Uint8 *data, Uint8 val, int pos)
{
	*(((Uint8 *)data)+pos)=val;
}

inline Uint8 getUint8(const Uint8 *data, int pos)
{
	return *(((Uint8 *)data)+pos);
}

inline void addSint8(const Uint8 *data, Sint8 val, int pos)
{
	*(((Uint8 *)data)+pos)=val;
}

inline Sint8 getSint8(const Uint8 *data, int pos)
{
	return *(((Sint8 *)data)+pos);
}

 
