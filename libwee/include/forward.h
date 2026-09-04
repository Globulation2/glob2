// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2004 Martin Voelkle <martin.voelkle@epfl.ch>

#ifndef FORWARD_H
#define FORWARD_H

#include <cstddef>
#include <stdint.h>

//#define self (*this)

struct Value;
struct Object;
struct Type;
namespace Types {
	struct ConstSize;
	struct VarSize;
};
struct Instruction;
typedef Instruction* Function;
typedef Function* VTable;

#endif // ndef FORWARD_H
