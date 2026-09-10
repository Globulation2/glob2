// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
class GenerationFailure : public std::runtime_error
{
  public:
	using std::runtime_error::runtime_error;
};
enum class GenerationError
{
	None,
	InvalidRequest,
	NonEmptyTarget,
	PlacementFailed,
	InvalidWorld
};
struct GenerationResult
{
	std::string generatorId;
	unsigned revision = 0;
	std::uint32_t seed = 0;
	std::string stage;
	GenerationError error = GenerationError::None;
	std::string detail;
	explicit operator bool() const { return error == GenerationError::None; }
	std::string diagnostic() const;
};
