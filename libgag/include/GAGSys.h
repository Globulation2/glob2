// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#ifndef MAX_SINT32
#define MAX_SINT32 0x7FFFFFFF
#endif

#ifdef WIN32

   #include <windows.h>
   #include <sys/types.h>
   #include <sys/stat.h>

   #define S_IFDIR _S_IFDIR

   #if defined(_MSC_VER) && _MSC_VER < 1900
	#define snprintf _snprintf
	#define vsnprintf _vsnprintf
	#pragma warning (disable : 4786)
	#pragma warning (disable : 4250)
   #endif

   #undef max
   #undef min

	#define HAVE_OPENGL
	#define _USE_MATH_DEFINES // To get M_PI
#endif

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
// This is the only one which should be left... In theory :-)
// Remove this comment once all other SDL deps have been removed.
#include <SDL.h>

// usefull macros
#ifndef MAX
#define MAX(a, b) ((a)>(b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b) ((a)<(b) ? (a) : (b))
#endif

#ifndef VARARRAY
#ifdef _MSC_VER
#include <malloc.h>
#define VARARRAY(t,n,s) t *n=(t*)_alloca((s)*sizeof(t))
#define strcasecmp _stricmp
#else
#define VARARRAY(t,n,s) t n[s]
#endif
#endif
