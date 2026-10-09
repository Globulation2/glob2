# Optimized normalization mechanism

GCC 13.4, -O3, ordinary release flags, retained symbols, no -pg. In normalizeX, the cached-mask comparison branches to an `and` and return when the current positive dimension matches the mask. The general path retains `idiv`, remainder correction and the previous nonpositive-dimension return. normalizeY has the equivalent structure. See normalizeX.asm and normalizeY.asm. The baseline disassembly is in serial-reanalysis/food-normalization-disassembly.txt on the evidence branch.

This establishes removal of integer division on prepared power-of-two inputs, not a numerical CPU speedup. The paired continuation screen supplies timing evidence separately.
