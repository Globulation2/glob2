/*
  Copyright (C) 2004 Martin Voelkle <martin.voelkle@epfl.ch>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "type.h"

#define TYPES_VALUES_BUILTIN(x) const Types::Builtin* Types::Builtin::x = new Types::Builtin(sizeof(Values::x))
TYPES_VALUES_BUILTIN(Void);
TYPES_VALUES_BUILTIN(Bool);
TYPES_VALUES_BUILTIN(Char);
TYPES_VALUES_BUILTIN(Nat);
TYPES_VALUES_BUILTIN(Int);
TYPES_VALUES_BUILTIN(Nat8);
TYPES_VALUES_BUILTIN(Int8);
TYPES_VALUES_BUILTIN(Nat16);
TYPES_VALUES_BUILTIN(Int16);
TYPES_VALUES_BUILTIN(Nat32);
TYPES_VALUES_BUILTIN(Int32);
TYPES_VALUES_BUILTIN(Nat64);
TYPES_VALUES_BUILTIN(Int64);
TYPES_VALUES_BUILTIN(ValueRef);
TYPES_VALUES_BUILTIN(ObjectRef);
