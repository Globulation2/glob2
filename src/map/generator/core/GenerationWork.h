// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace MapGeneration
{
// Optional, invocation-local accounting. Ordinary native generation has no budget.
struct GenerationWork
{
	void *host;
	void (*charge)(void *, std::uint64_t);
	void (*allocate)(void *, std::uint64_t) = nullptr;
};
inline thread_local GenerationWork *generationWork = nullptr;
inline void generationCheckpoint(std::uint64_t units = 1)
{
	if (generationWork)
		generationWork->charge(generationWork->host, units);
}
inline void generationAllocation(std::uint64_t bytes)
{
	if (generationWork && generationWork->allocate)
		generationWork->allocate(generationWork->host, bytes);
}
class GenerationWorkScope
{
	GenerationWork *previous;

  public:
	explicit GenerationWorkScope(GenerationWork &work) : previous(generationWork)
	{
		generationWork = &work;
	}
	~GenerationWorkScope() { generationWork = previous; }
};
} // namespace MapGeneration
