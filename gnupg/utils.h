/* util.h
 *	Copyright (C) 1998, 1999, 2000, 2001 Free Software Foundation, Inc.
 *
 * This file is part of GNUPG.
 *
 * GNUPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GNUPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

/*  Based on gnupg-1.2.4/cipher/util.h
 *  Took a subset to easyly integrate into glob2
 *  2004 by Luc-Olivier de Charrière Nuage@ysagoon.com
 */
 
#ifndef G10_UTIL_H
#define G10_UTIL_H

#define DIM(v) (sizeof(v)/sizeof((v)[0]))

#define wipememory2(_ptr,_set,_len) do { volatile char *_vptr=(volatile char *)(_ptr); size_t _vlen=(_len); while(_vlen) { *_vptr=(_set); _vptr++; _vlen--; } } while(0)
#define wipememory(_ptr,_len) wipememory2(_ptr,0,_len)

#define rol(n,x) ( ((x) << (n)) | ((x) >> (32-(n))) )

#endif /*G10_UTIL_H*/
