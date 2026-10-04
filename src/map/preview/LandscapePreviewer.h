// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GUIMapPreview.h"
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
	friend struct CustomGameSetupHarness;

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
		/// How good a start each colony got, as GenerationService scores it; 0 unless Ready.
		double score = 0;
		/// The last generation diagnostic, when Failed.
		std::string detail;
		/// Increments on every state change, so a viewer polling cheaply knows when to copy.
		unsigned revision = 0;
	};
	static constexpr int kAttempts = 3;
	/// Zero picks a hardware-derived worker count; negative uses cooperative poll() on any host.
	/// A deferred queue waits for prioritize() so its first jobs can follow the initial layout.
	explicit LandscapePreviewer(std::vector<GenerationRequest> requests, int threads = 0,
								bool deferred = false);
	~LandscapePreviewer();
	LandscapePreviewer(const LandscapePreviewer &) = delete;
	LandscapePreviewer &operator=(const LandscapePreviewer &) = delete;
	/// Rolls every request again with fresh seeds. Rolls already under way finish, then are
	/// dropped rather than shown.
	void regenerate();
	/// Advance one generation attempt on hosts without worker threads; retries yield too.
	void poll();
	/// Cooperative hosts may restrict an attempt to cards actually in view.
	void poll(const std::vector<std::size_t> &eligible);
	/// Reorder pending work without rerolling. Unlisted slots follow listed slots in index order.
	/// Also releases a deferred queue. Already running attempts are not interrupted.
	void prioritize(const std::vector<std::size_t> &order);
	/// Replaces the requests and rolls them with fresh seeds, on the same workers; rolls of the
	/// old requests already under way finish, then are dropped.
	void restart(std::vector<GenerationRequest> requests);
	/// Replaces one slot's request and rolls that slot again with a fresh seed, leaving the others
	/// as they are: how a picker redraws a randomised landscape whose parameters the world refused.
	void reroll(std::size_t index, GenerationRequest request);
	/// The request a slot is currently showing (or rolling).
	GenerationRequest request(std::size_t index) const;
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
	static Preview rollAttempt(const GenerationRequest &, std::uint32_t rootSeed, int attempt);
	void beginPass();
	void sortQueue();
	void pollNext(const std::vector<std::size_t> *eligible);
	void advance(std::unique_lock<std::mutex> &lock);
	void work();
	std::vector<GenerationRequest> requests;
	std::vector<Preview> slots;
	std::vector<std::uint32_t> seeds;
	std::vector<int> attempts;
	std::vector<std::size_t> priorities;
	bool started;
	/// Per slot, the pass its pending roll belongs to: a roll finished for an older pass is dropped.
	std::vector<unsigned> passes;
	unsigned pass = 0;
	/// Slots waiting for a worker, in order.
	std::vector<std::size_t> queue;
	bool stopping = false;
	mutable std::mutex mutex;
	std::condition_variable wake;
	std::vector<std::thread> workers;
};
