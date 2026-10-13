// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapState.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace gradient_kernel::residency
{
// This descriptor is derived from the authoritative record definitions. The
// initial GPU decoder supports one explicit little-endian representation.
// A different ABI must decline, rather than reinterpret an uploaded record.
struct RecordRepresentation
{
	std::size_t resourceStride, resourceOffset, typeOffset, amountOffset;
	std::size_t occupancyStride, buildingOffset, immobileOffset;
	std::size_t areaStride, forbiddenOffset, clearOffset, farmOffset;
	bool littleEndian;
	bool operator==(const RecordRepresentation &) const = default;
};
inline constexpr RecordRepresentation nativeRecordRepresentation{
	sizeof(MapState::ResourceCell),
	offsetof(MapState::ResourceCell, resource),
	offsetof(Resource, type),
	offsetof(Resource, amount),
	sizeof(MapState::OccupancyCell),
	offsetof(MapState::OccupancyCell, building),
	offsetof(MapState::OccupancyCell, immobileUnit),
	sizeof(MapState::AreaCell),
	offsetof(MapState::AreaCell, forbidden),
	offsetof(MapState::AreaCell, clear),
	offsetof(MapState::AreaCell, farm),
	std::endian::native == std::endian::little};
inline constexpr RecordRepresentation initialGPURepresentation{12, 0,  0, 4, 8,  0,
															   6,  16, 0, 8, 12, true};
inline bool
supportsPreparationRecords(const RecordRepresentation &representation = nativeRecordRepresentation)
{
	return representation == initialGPURepresentation;
}
inline std::optional<std::uint32_t> decodeLittleEndian(std::span<const std::byte> bytes,
													   std::size_t offset, unsigned width)
{
	if ((width != 1 && width != 2 && width != 4) || offset > bytes.size() ||
		width > bytes.size() - offset)
		return {};
	std::uint32_t result = 0;
	for (unsigned i = 0; i < width; ++i)
		result |= std::uint32_t(std::to_integer<unsigned char>(bytes[offset + i])) << (8 * i);
	return result;
}
} // namespace gradient_kernel::residency
