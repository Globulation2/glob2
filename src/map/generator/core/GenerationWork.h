// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
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
// Scopes open on any thread. Checkpoints sit in the innermost loops of every primitive, and
// reading a thread_local is a function call on some platforms (macOS), so native generation,
// which never opens a scope, tests this plain counter instead. A thread that opened a scope
// incremented it itself, so it always goes on to its own budget: charging is unchanged.
inline std::atomic<unsigned> generationWorkScopes{0};
inline bool generationWorkActive()
{
	return generationWorkScopes.load(std::memory_order_relaxed) && generationWork;
}
inline void generationCheckpoint(std::uint64_t units = 1)
{
	if (generationWorkActive())
		generationWork->charge(generationWork->host, units);
}
inline void generationAllocation(std::uint64_t bytes)
{
	if (generationWorkActive() && generationWork->allocate)
		generationWork->allocate(generationWork->host, bytes);
}
class GenerationWorkScope
{
	GenerationWork *previous;

  public:
	explicit GenerationWorkScope(GenerationWork &work) : previous(generationWork)
	{
		generationWorkScopes.fetch_add(1, std::memory_order_relaxed);
		generationWork = &work;
	}
	~GenerationWorkScope()
	{
		generationWork = previous;
		generationWorkScopes.fetch_sub(1, std::memory_order_relaxed);
	}
	GenerationWorkScope(const GenerationWorkScope &) = delete;
	GenerationWorkScope &operator=(const GenerationWorkScope &) = delete;
};
} // namespace MapGeneration
