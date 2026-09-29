// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationContext.h"
#include "GenerationRequest.h"
#include <map>
#include <random>
#include <string>
namespace MapGeneration
{
/// A generation asks for the same design several times (the request check, generate and validateWorld),
/// and building it can be most of a generation's time. A design depends only on the request and the
/// named streams it draws from, so the last one built on this thread, in a context of its own, is kept
/// and handed out again for the same request (nbWorkers plays no part in it): its telemetry replayed
/// into the asking context, and every stream it drew from wound on to where building it left that
/// stream, so whatever the context draws next is unchanged. The streams are the ones the building
/// context asked for, so a new draw in the design needs no list kept by hand. A context that has
/// already drawn from one of those streams gets the design built afresh from where its streams stand.
///
/// One cache per Layout type, which is one per generator: every generator's Layout is its own type.
/// Karst towers, Central Quarry and Hidden Oasis.
template <typename Layout>
Layout cachedDesign(const GenerationRequest &request, GenerationContext &context,
					Layout (*designAfresh)(const GenerationRequest &, GenerationContext &))
{
	struct Cache
	{
		bool valid = false;
		GenerationRequest request;
		Layout layout;
		GenerationTelemetry telemetry;
		std::map<std::string, std::mt19937> streams; // each as the design left it
	};
	thread_local Cache cache;
	const GenerationRequest &was = cache.request;
	if (!cache.valid || was.method != request.method || was.wDec != request.wDec ||
		was.hDec != request.hDec || was.nbTeams != request.nbTeams || was.seed != request.seed ||
		was.options != request.options)
	{
		cache.valid = false;
		GenerationContext fresh(request, true);
		cache.layout = designAfresh(request, fresh);
		cache.telemetry = fresh.telemetry;
		cache.streams = fresh.namedStreams();
		cache.request = request;
		cache.valid = true;
	}
	for (const auto &[name, state] : cache.streams)
		if (context.stream(name) != std::mt19937(GenerationContext::deriveSeed(request.seed, name)))
			return designAfresh(request, context);
	for (const auto &[name, state] : cache.streams)
		context.stream(name) = state;
	context.telemetry.replay(cache.telemetry);
	return cache.layout;
}
} // namespace MapGeneration
