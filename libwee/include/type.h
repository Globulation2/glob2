/*
  Copyright (C) 2004 Martin Voelkle <martin.voelkle@epfl.ch>

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

#ifndef TYPE_H
#define TYPE_H

#include "value.h"
#include <memory>
#include <ext/hash_map>

struct Interface: Value {
};

struct Type: Interface {
	// meta type
	// TODO: this enum is redundant with the virtual table
	// find a way to switch on real types (maybe with RTTI)
	// A visitor pattern is too slow (2 virtual calls)
	const enum Meta {
		Builtin,
		Compound,
		Array,
		VarArray,
	} meta;
	virtual size_t Size(const Value* value) const = 0;
	Type(Meta meta): Interface(), meta(meta) {}
	virtual ~Type() {}
	__gnu_cxx::hash_map<const Interface*, const VTable*> interfaces;
};

namespace Types {

	// Fixed size type
	struct ConstSize: Type {
		// storage size in bytes
		const size_t size;
		size_t Size(const Value* value) const { return size; }

		ConstSize(Meta meta, size_t size): Type(meta), size(size) {}
		::Value* Copy(const Value* src, Value* dest) const {
			const uint8_t* srcBegin = (const uint8_t*)src;
			uint8_t* destBegin = (uint8_t*)dest;
			std::copy(srcBegin, srcBegin + size, destBegin);
			return (::Value*)destBegin + size;
		}
	};

	// Builtin types
	struct Builtin: ConstSize {
		// builtin types
		static const Builtin* Void;
		static const Builtin* Bool;
		static const Builtin* Char;
		static const Builtin* Nat;
		static const Builtin* Int;
		static const Builtin* Nat8;
		static const Builtin* Int8;
		static const Builtin* Nat16;
		static const Builtin* Int16;
		static const Builtin* Nat32;
		static const Builtin* Int32;
		static const Builtin* Nat64;
		static const Builtin* Int64;
		static const Builtin* ValueRef;
		static const Builtin* ObjectRef;
	private:
		Builtin(size_t size): ConstSize(Type::Builtin, size) {}
	};

	// Compound type
	struct Compound: ConstSize {
		// for reflection & garbage collection
		const size_t fieldsCount;
		struct Field {
			ConstSize* type;
		};
		const Field* fields;
	private:
		template<typename Iter>
		static size_t TotalSize(Iter begin, Iter end) {
			size_t acc = 0;
			for(Iter iter = begin; iter != end; ++iter) {
				acc += (*iter).type->size;
			}
			return acc;
		}
		Compound(size_t fieldsCount, const Field* fields): ConstSize(Type::Compound, TotalSize(fields, fields + fieldsCount)), fieldsCount(fieldsCount), fields(fields) {}
	};

	// Array type
	struct Array: ConstSize {
		const ConstSize* elemsType;
		const size_t elemsCount;
	private:
		Array(const ConstSize* elemsType, const size_t elemsCount): ConstSize(Type::Array, elemsCount * elemsType->size), elemsType(elemsType), elemsCount(elemsCount) {}
	};

	// Variable size type
	struct VarSize: Type {
		// storage size in bytes
		virtual size_t Size(const Value* value) const = 0;

		VarSize(Meta meta): Type(meta) {}
		::Value* Copy(const Value* src, Value* dest) const {
			size_t size = Size(src);
			const uint8_t* srcBegin = (const uint8_t*)src;
			uint8_t* destBegin = (uint8_t*)dest;
			std::copy(srcBegin, srcBegin + size, destBegin);
			return (::Value*)destBegin + size;
		}
	};

	// Variable length array type
	struct VarArray: VarSize {
		const ConstSize* elemsType;
	private:
		VarArray(const ConstSize* elemsType): VarSize(Type::VarArray), elemsType(elemsType) {}
	};

};

#endif // ndef TYPE_H
