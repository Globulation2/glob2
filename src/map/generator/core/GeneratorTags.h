// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>

/// The landscape picker's browsing facets: every built-in generator's tags, curated here in one
/// place rather than in each generator's own file, so the catalog stays easy to review and keep
/// consistent (see GeneratorRegistry, which assigns these onto GeneratorDefinition::tags).
/// Each tag is "category:value" (a fixed small set of categories, an open set of values) so the
/// landscape picker's filter row can group them into per-category dropdowns, Amazon-catalog
/// style: "terrain" (the overall character - natural, arena, urban), "feature" (a specific
/// element a player would recognise - river, islands, stone walls...), "style" (the kind of game
/// it tends to produce - tight-building, wide-open, siege...), and "fairness" (how starts relate
/// to each other geometrically). A generator may carry several values in the same category.
namespace GeneratorTags
{
/// This generator's curated tags, by its GeneratorDefinition::id. Empty only for an id this table
/// does not (yet) know - GeneratorRegistry's constructor rejects that for any non-editor-only
/// generator, so a newly added generator must get a row here.
std::vector<std::string> tagsFor(const std::string &id);

/// Every category this table uses, in the order the filter row should show them.
std::vector<std::string> categories();

/// Every distinct value tagged under `category` across all generators (e.g. categories()[0] might
/// be "terrain" and this returns {"natural", "arena", "urban"} for it), sorted for a stable menu.
std::vector<std::string> valuesFor(const std::string &category);
} // namespace GeneratorTags
