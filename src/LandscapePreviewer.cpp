// SPDX-License-Identifier: GPL-3.0-or-later
#include "LandscapePreviewer.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationService.h"
#include <algorithm>
#include <iostream>
#include <ThreadSupport.h>

LandscapePreviewer::LandscapePreviewer(std::vector<GenerationRequest> requests, int threads,
									   bool deferred)
	: requests(std::move(requests)), started(!deferred)
{
	slots.resize(this->requests.size());
	seeds.resize(this->requests.size());
	passes.resize(this->requests.size());
	attempts.resize(this->requests.size());
	regenerate();
	if constexpr (GAGCore::ThreadSupport::available)
	{
		if (threads == 0)
		{
			const unsigned cores = std::thread::hardware_concurrency();
			threads = int(std::clamp(cores == 0 ? 1u : cores - 1, 1u, 4u));
		}
		threads = std::min(std::max(0, threads), int(std::max<std::size_t>(1, this->requests.size())));
		try
		{
			for (int i = 0; i < threads; ++i)
				workers.push_back(GAGCore::ThreadSupport::launch([this] { work(); }));
		}
		catch (const std::system_error &)
		{
			{ std::lock_guard<std::mutex> lock(mutex); stopping = true; }
			wake.notify_all();
			for (auto &worker : workers) worker.join();
			workers.clear();
			stopping = false; // remaining candidates use the cooperative path
		}
	}

}

LandscapePreviewer::~LandscapePreviewer()
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		stopping = true;
	}
	wake.notify_all();
	for (auto &worker : workers)
		worker.join();
}

// Caller holds the mutex.
void LandscapePreviewer::beginPass()
{
	++pass;
	queue.clear();
	const auto root = GenerationContext::randomSeed();
	for (std::size_t i = 0; i < slots.size(); ++i)
	{
		seeds[i] = GenerationContext::deriveSeed(root, "landscape/" + std::to_string(i));
		passes[i] = pass;
		attempts[i] = 0;
		slots[i].state = State::Pending;
		++slots[i].revision;
		queue.push_back(i);
	}
	sortQueue();
}

void LandscapePreviewer::regenerate()
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		beginPass();
	}
	wake.notify_all();
}

void LandscapePreviewer::restart(std::vector<GenerationRequest> fresh)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		requests = std::move(fresh);
		slots.resize(requests.size());
		seeds.resize(requests.size());
		passes.resize(requests.size());
		attempts.resize(requests.size());
		beginPass();
	}
	wake.notify_all();
}

void LandscapePreviewer::reroll(std::size_t index, GenerationRequest request)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (index >= slots.size())
			return;
		// A pass of its own, so a roll of this slot still under way is dropped when it lands.
		++pass;
		requests[index] = std::move(request);
		seeds[index] = GenerationContext::deriveSeed(GenerationContext::randomSeed(),
													 "landscape/" + std::to_string(index));
		passes[index] = pass;
		attempts[index] = 0;
		slots[index].state = State::Pending;
		++slots[index].revision;
		queue.erase(std::remove(queue.begin(), queue.end(), index), queue.end());
		queue.push_back(index);
		sortQueue();
	}
	wake.notify_all();
}

GenerationRequest LandscapePreviewer::request(std::size_t index) const
{
	std::lock_guard<std::mutex> lock(mutex);
	return requests.at(index);
}

unsigned LandscapePreviewer::revision(std::size_t index) const
{
	std::lock_guard<std::mutex> lock(mutex);
	return slots.at(index).revision;
}

LandscapePreviewer::Preview LandscapePreviewer::preview(std::size_t index) const
{
	std::lock_guard<std::mutex> lock(mutex);
	return slots.at(index);
}

bool LandscapePreviewer::busy() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return std::any_of(slots.begin(), slots.end(), [](const Preview &p)
					   { return p.state == State::Pending || p.state == State::Generating; });
}

int LandscapePreviewer::finished() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return int(std::count_if(slots.begin(), slots.end(), [](const Preview &p)
							 { return p.state == State::Ready || p.state == State::Failed; }));
}

void LandscapePreviewer::sortQueue()
{
	if (priorities.size() != slots.size())
		return;
	std::sort(queue.begin(), queue.end(),
			  [this](std::size_t a, std::size_t b) { return priorities[a] < priorities[b]; });
}

void LandscapePreviewer::prioritize(const std::vector<std::size_t> &order)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		const auto count = slots.size();
		priorities.assign(count, count);
		std::size_t rank = 0;
		for (auto index : order)
			if (index < count && priorities[index] == count)
				priorities[index] = rank++;
		for (auto &priority : priorities)
			if (priority == count)
				priority = rank++;
		sortQueue();
		started = true;
	}
	wake.notify_all();
}

LandscapePreviewer::Preview LandscapePreviewer::rollAttempt(const GenerationRequest &request,
															std::uint32_t rootSeed, int attempt)
{
	Preview result;
	GenerationService generator;
	auto roll = request;
	roll.seed = GenerationContext::deriveSeed(rootSeed, "attempt/" + std::to_string(attempt));
	Game game(nullptr);
	const auto outcome = generator.generate(game, roll);
	if (!outcome || game.teamsCount() != request.nbTeams)
	{
		result.detail = outcome ? "Could not place every colony" : outcome.diagnostic();
		if (outcome.error == GenerationError::InvalidRequest || attempt + 1 == kAttempts)
		{
			std::cerr << "Landscape preview: " << result.detail << std::endl;
			result.state = State::Failed;
		}
		return result; // Pending retries return to the queue between attempts.
	}
	result.thumbnail.loadFromMap(game.map);
	for (int i = 0; i < game.teamsCount(); ++i)
		result.starts.push_back(
			{game.teams[i]->startPosX, game.teams[i]->startPosY, game.teams[i]->color});
	result.seed = roll.seed;
	result.score = outcome.quality.score;
	result.width = game.map.getW();
	result.height = game.map.getH();
	result.state = State::Ready;
	return result;
}

LandscapePreviewer::Preview LandscapePreviewer::roll(const GenerationRequest &request,
													 std::uint32_t rootSeed)
{
	for (int attempt = 0; attempt < kAttempts; ++attempt)
	{
		auto result = rollAttempt(request, rootSeed, attempt);
		if (result.state != State::Pending)
			return result;
	}
	return {}; // rollAttempt makes the final attempt terminal.
}

// Entered and returned with the mutex held. Expensive work never holds the UI's lock.
void LandscapePreviewer::advance(std::unique_lock<std::mutex> &lock)
{
	const std::size_t index = queue.front();
	queue.erase(queue.begin());
	const unsigned myPass = passes[index];
	const int attempt = attempts[index]++;
	slots[index].state = State::Generating;
	++slots[index].revision;
	const GenerationRequest request = requests[index];
	const std::uint32_t seed = seeds[index];
	lock.unlock();
	Preview result = rollAttempt(request, seed, attempt);
	lock.lock();
	if (index >= passes.size() || passes[index] != myPass)
		return;
	if (result.state == State::Pending)
	{
		// Retain an old image while retrying, as beginPass does while regenerating.
		slots[index].state = State::Pending;
		slots[index].detail = std::move(result.detail);
		++slots[index].revision;
		queue.push_back(index);
		sortQueue();
		wake.notify_one();
	}
	else
	{
		result.revision = slots[index].revision + 1;
		slots[index] = std::move(result);
	}
}

void LandscapePreviewer::work()
{
	std::unique_lock<std::mutex> lock(mutex);
	for (;;)
	{
		wake.wait(lock, [this] { return stopping || (started && !queue.empty()); });
		if (stopping)
			return;
		advance(lock);
	}
}

void LandscapePreviewer::poll()
{
	pollNext(nullptr);
}

void LandscapePreviewer::poll(const std::vector<std::size_t> &eligible)
{
	pollNext(&eligible);
}

void LandscapePreviewer::pollNext(const std::vector<std::size_t> *eligible)
{
	if (!workers.empty())
		return;
	std::unique_lock<std::mutex> lock(mutex);
	if (!started || stopping || queue.empty())
		return;
	if (eligible)
	{
		const auto next = std::find_if(
			queue.begin(), queue.end(), [&](std::size_t index)
			{ return std::find(eligible->begin(), eligible->end(), index) != eligible->end(); });
		if (next == queue.end())
			return;
		std::rotate(queue.begin(), next, next + 1);
	}
	advance(lock);
}
