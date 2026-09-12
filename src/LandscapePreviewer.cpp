// SPDX-License-Identifier: GPL-3.0-or-later
#include "LandscapePreviewer.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationService.h"
#include <algorithm>
#include <iostream>

LandscapePreviewer::LandscapePreviewer(std::vector<GenerationRequest> requests, int threads)
	: requests(std::move(requests))
{
	slots.resize(this->requests.size());
	seeds.resize(this->requests.size());
	if (threads <= 0)
	{
		const unsigned cores = std::thread::hardware_concurrency();
		threads = int(std::clamp(cores == 0 ? 1u : cores - 1, 1u, 4u));
	}
	threads = int(std::min<std::size_t>(threads, std::max<std::size_t>(1, this->requests.size())));
	regenerate();
	for (int i = 0; i < threads; ++i)
		workers.emplace_back([this] { work(); });
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

void LandscapePreviewer::regenerate()
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		++pass;
		next = 0;
		const auto root = GenerationContext::randomSeed();
		for (std::size_t i = 0; i < slots.size(); ++i)
		{
			seeds[i] = GenerationContext::deriveSeed(root, "landscape/" + std::to_string(i));
			slots[i].state = State::Pending;
			++slots[i].revision;
		}
	}
	wake.notify_all();
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

LandscapePreviewer::Preview LandscapePreviewer::roll(const GenerationRequest &request,
													 std::uint32_t rootSeed)
{
	Preview result;
	GenerationService generator;
	for (int attempt = 0; attempt < kAttempts; ++attempt)
	{
		auto roll = request;
		roll.seed = GenerationContext::deriveSeed(rootSeed, "attempt/" + std::to_string(attempt));
		Game game(nullptr);
		const auto outcome = generator.generate(game, roll);
		if (!outcome || game.teamsCount() != request.nbTeams)
		{
			result.detail = outcome ? "Could not place every colony" : outcome.diagnostic();
			// A request the generator refuses outright fails the same way on every seed.
			if (outcome.error == GenerationError::InvalidRequest)
				break;
			continue;
		}
		result.thumbnail.loadFromMap(game.map);
		for (int i = 0; i < game.teamsCount(); ++i)
			result.starts.push_back(
				{game.teams[i]->startPosX, game.teams[i]->startPosY, game.teams[i]->color});
		result.seed = roll.seed;
		result.width = game.map.getW();
		result.height = game.map.getH();
		result.state = State::Ready;
		result.detail.clear();
		return result;
	}
	std::cerr << "Landscape preview: " << result.detail << std::endl;
	result.state = State::Failed;
	return result;
}

void LandscapePreviewer::work()
{
	std::unique_lock<std::mutex> lock(mutex);
	for (;;)
	{
		wake.wait(lock, [this] { return stopping || next < requests.size(); });
		if (stopping)
			return;
		const std::size_t index = next++;
		const unsigned myPass = pass;
		slots[index].state = State::Generating;
		++slots[index].revision;
		const GenerationRequest request = requests[index];
		const std::uint32_t seed = seeds[index];
		lock.unlock();
		Preview result = roll(request, seed);
		lock.lock();
		if (pass != myPass)
			continue; // regenerate() superseded this roll while it ran
		result.revision = slots[index].revision + 1;
		slots[index] = std::move(result);
	}
}
