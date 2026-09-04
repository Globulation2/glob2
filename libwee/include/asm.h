// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2004 Martin Voelkle <martin.voelkle@epfl.ch>

#ifndef ASM_H
#define ASM_H

#include "container.h"

namespace Asm {
	
	struct Instruction {
		// returns next instruction to evaluate
#define ASM_INSTRUCTION_NEXT (const Instruction*)(((const uint8_t*)this) + sizeof(*this))
		virtual const Instruction* Eval(Stack* stack) const = 0;
		virtual ~Instruction() { }
	};
	
	namespace Instructions {
		
		struct Nop: Instruction {
			const Instruction* Eval(Stack* stack) const {
				return ASM_INSTRUCTION_NEXT;
			}
		};
		
		// Nop instruction with metadata
		template<typename T>
		struct Meta: Instruction {
			const T data;
			Meta(const T& data): data(data) {}
			const Instruction* Eval(Stack* stack) const {
				return ASM_INSTRUCTION_NEXT; // Nop
			}
		};
		
		// Load immediate constant
		// ... => ..., value
		struct Const: Instruction {
			const size_t size;
			const uint8_t* source;
			Const(size_t size, uint8_t* source): size(size), source(source) {}
			const Instruction* Eval(Stack* stack) const {
				uint8_t* dest = stack->Alloc(size);
				std::copy(source, source + size, dest);
				return ASM_INSTRUCTION_NEXT;
			}
		};
		
		// Duplicate value
		// ..., value => ..., value, value
		struct Duplicate: Instruction {
			const size_t size;
			Duplicate(size_t size): size(size) {}
			const Instruction* Eval(Stack* stack) const {
				const uint8_t* source = stack->Get();
				uint8_t* dest = stack->Alloc(size);
				std::copy(source, source + size, dest);
				return ASM_INSTRUCTION_NEXT;
			}
		};
		
		// Load from address
		// ..., address => ..., value
		struct Load: Instruction {
			const size_t size;
			Load(size_t size): size(size) {}
			const Instruction* Eval(Stack* stack) const {
				const uint8_t* source;
				stack->Pop(&source);
				uint8_t* dest = stack->Alloc(size);
				std::copy(source, source + size, dest);
				return ASM_INSTRUCTION_NEXT;
			}
		};
		
		// Store to address
		// ..., address, value => ...
		struct Store: Instruction {
			const size_t size;
			Store(size_t size): size(size) {}
			const Instruction* Eval(Stack* stack) const {
				const uint8_t* source = stack->Get();
				uint8_t* dest = *(uint8_t**)stack->Get(size);
				std::copy(source, source + size, dest);
				stack->Free(size + sizeof(dest));
				return ASM_INSTRUCTION_NEXT;
			}
		};
		
		// Copy data
		// ..., destAddress, srcAddress => ...
		struct Copy: Instruction {
			const size_t size;
			Copy(size_t size): size(size) {}
			const Instruction* Eval(Stack* stack) const {
				const uint8_t* source;
				stack->Pop(&source);
				uint8_t* dest;
				stack->Pop(&dest);
				std::copy(source, source + size, dest);
				return ASM_INSTRUCTION_NEXT;
			}
		};
		
		// Build stack reference
		// ... => ..., address
		struct Reference: Instruction {
			const size_t offset;
			Reference(size_t offset): offset(offset) {}
			const Instruction* Eval(Stack* stack) const {
				stack->Push(stack->Get(offset));
				return ASM_INSTRUCTION_NEXT;
			}
		};
		
		// Grow stack
		// ... => ..., space
		struct Alloc: Instruction {
			const size_t size;
			Alloc(size_t size): size(size) {}
			const Instruction* Eval(Stack* stack) const {
				stack->Alloc(size);
				return ASM_INSTRUCTION_NEXT;
			}
		};
		
		// Shrink stack
		// ..., space => ...
		struct Free: Instruction {
			const size_t size;
			Free(size_t size): size(size) {}
			const Instruction* Eval(Stack* stack) const {
				stack->Free(size);
				return ASM_INSTRUCTION_NEXT;
			}
		};
		
		// Jump to target instruction
		// ..., target => ...
		struct Jump: Instruction {
			const Instruction* Eval(Stack* stack) const {
				const Instruction* next;
				stack->Pop(&next);
				return next;
			}
		};
		
		// Call function
		// ..., target => ..., returnAddress
		struct Call: Instruction {
			const Instruction* Eval(Stack* stack) const {
				const Instruction* next;
				stack->Pop(&next);
				stack->Push(ASM_INSTRUCTION_NEXT);
				return next;
			}
		};
		
		// Return from function
		// ..., returnAddress => ...
		typedef Jump Return;
		
	};
	
};

#endif // ndef ASM_H
