// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "LobbyMapPreview.h"
#include "MapThumbnail.h"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/// Rolls one preview map per request on background threads, so a picker can show every
/// landscape side by side while the UI keeps running. A slot is rolled with derived seeds until
/// a roll succeeds or kAttempts are spent, and the seed that succeeded is reported, so a caller
/// can regenerate exactly the map it showed. Generation is deterministic per seed, and each
/// thread has its own synchronized random stream, so any number of workers may roll at once.
class LandscapePreviewer
{
  public:
	enum class State
	{
		Pending,
		Generating,
		Ready,
		Failed
	};
	struct Preview
	{
		State state = State::Pending;
		MapThumbnail thumbnail;
		std::vector<MapStart> starts;
		std::uint32_t seed = 0;
		int width = 0, height = 0;
		/// The last generation diagnostic, when Failed.
		std::string detail;
		/// Increments on every state change, so a viewer polling cheaply knows when to copy.
		unsigned revision = 0;
	};
	static constexpr int kAttempts = 3;
	/// threads <= 0 picks a count from the hardware, leaving a core to the UI.
	explicit LandscapePreviewer(std::vector<GenerationRequest> requests, int threads = 0);
	~LandscapePreviewer();
	LandscapePreviewer(const LandscapePreviewer &) = delete;
	LandscapePreviewer &operator=(const LandscapePreviewer &) = delete;
	/// Rolls every request again with fresh seeds. Rolls already under way finish, then are
	/// dropped rather than shown.
	void regenerate();
	std::size_t size() const { return requests.size(); }
	unsigned revision(std::size_t index) const;
	Preview preview(std::size_t index) const;
	/// True while any slot is still pending or being rolled.
	bool busy() const;
	/// Slots that have reached Ready or Failed in the current pass.
	int finished() const;
	int threadCount() const { return int(workers.size()); }
	/// One slot's roll, on the calling thread; what the workers run.
	static Preview roll(const GenerationRequest &request, std::uint32_t rootSeed);

  private:
	void work();
	std::vector<GenerationRequest> requests;
	std::vector<Preview> slots;
	std::vector<std::uint32_t> seeds;
	unsigned pass = 0;
	std::size_t next = 0;
	bool stopping = false;
	mutable std::mutex mutex;
	std::condition_variable wake;
	std::vector<std::thread> workers;
};
