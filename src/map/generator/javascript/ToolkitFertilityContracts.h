// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ToolkitBinding.h"
#include "map/FertilityField.h"
#include "GenerationFertilityWork.h"

namespace MapGeneration::JavaScript
{
// Fertility also serves live simulation, outside the instrumented generator toolkit.
// Keep its script-only checks and conservative reservations at the binding boundary.
// Reservations are cumulative, including temporary allocations, just like other
// native toolkit work; native calls must never execute before these checks succeed.
template <class A, class B>
void prepareFertilityRebuild(Binding &e, int width, int height, const std::vector<A> &source,
							 const std::vector<B> &inhibition, Fertility::Path path)
{
	if (width <= 0 || height <= 0 || width > 1024 || height > 1024)
		throw TypeMismatch("Fertility dimensions must be 1..1024");
	const auto tiles = std::uint64_t(width) * height;
	if (source.size() != tiles || inhibition.size() != tiles)
		throw TypeMismatch("Fertility inputs must match width * height");
	if (path != Fertility::Path::Adaptive && path != Fertility::Path::SandCorrection &&
		path != Fertility::Path::WaterSplat)
		throw TypeMismatch("Invalid fertility path");

	// Reserve directly: unit tests can call adapters without a native work scope.
	e.chargeNative(tiles * 2048);
	e.allocate(tiles * 32 + std::uint64_t(height) * (width + 60) * 2);
}

inline void prepareFertilityRead(const Fertility::Field &field)
{
	if (field.getW() <= 0 || field.getH() <= 0 ||
		field.values().size() != std::uint64_t(field.getW()) * field.getH())
		throw TypeMismatch("Fertility field must be rebuilt before reading");
}

template <class T>
void prepareFertilityMask(Binding &e, const Fertility::Field &field, const std::vector<T> &mask)
{
	prepareFertilityRead(field);
	if (mask.size() != field.values().size())
		throw TypeMismatch("Fertility mask must match field dimensions");
	e.chargeNative(mask.size());
}
} // namespace MapGeneration::JavaScript
