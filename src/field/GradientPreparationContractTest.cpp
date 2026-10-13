// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "GradientPreparationRules.h"
#include "GradientResidency.h"
#include "GradientRecordRepresentation.h"
#include "MapState.h"
#include "CellRules.h"
#include <bit>
#include <cstddef>
#include <limits>
#include <type_traits>

TEST_SUITE("GradientPreparationContract")
{
	TEST_CASE("Generated predicates preserve independent seed precedence")
	{
		using namespace gradient_preparation::rules;
		for (unsigned bits = 0; bits < 1024; ++bits)
			for (const unsigned terrain : {0u, 1u})
			{
				const auto flag = [&](unsigned n) { return unsigned((bits >> n) & 1); };
				unsigned material = 0;
				if (!flag(0) && flag(1))
				{
					if (!flag(2) && !flag(3))
						material = terrain;
					if (flag(5) && flag(6))
						material = 65535;
				}
				CHECK(materialSeed(flag(0), flag(1), flag(2), flag(3), terrain, flag(5), flag(6)) ==
					  material);
				unsigned clearing = 0;
				if (!flag(0))
				{
					if (flag(4) && (flag(7) || (flag(9) && flag(8) && !flag(5))))
						clearing = 65535;
					else if (!flag(2) && !flag(3) && flag(1))
						clearing = terrain;
				}
				CHECK(clearSeed(flag(0), flag(1), flag(2), flag(3), terrain, flag(4), flag(5),
								flag(7), flag(8), flag(9)) == clearing);
				// An open placeholder defers terrain without changing precedence.
				// Use the independent reference above, including clearing goals
				// that override buildings and immobile units.
				const auto classified = clearSeed(flag(0), flag(1), flag(2), flag(3),
					GRADIENT_UNREACHABLE, flag(4), flag(5), flag(7), flag(8), flag(9));
				CHECK((classified == GRADIENT_UNREACHABLE ? terrain : classified) == clearing);
			}
		for (unsigned mask = 0; mask <= AllMaterials; ++mask)
			CHECK(resourceTraits(mask, 1, 1, 1, 1) == (mask | 0xf0000u));
	}
	TEST_CASE("Shared residency model preserves exact versions and retirement")
	{
		using namespace gradient_kernel::residency;
		// This tests the common model, not adapter bindings or GPU execution.
		auto owner = std::make_shared<unsigned>(1);
		Identity first{17, 1, 3, 128, 128, 2, owner, owner, {1, 1}};
		Buffer buffer;
		CHECK_FALSE(buffer.acquire(first));
		const auto upload = buffer.begin(first);
		REQUIRE(upload != 0);
		CHECK_FALSE(buffer.acquire(first));
		CHECK(buffer.complete(upload, true, true));
		REQUIRE(buffer.acquire(first));
		auto second = first;
		second.componentVersion = 2;
		second.chunks[1] = 2;
		CHECK(buffer.begin(second) == 0);
		buffer.release();
		const auto next = buffer.begin(second);
		REQUIRE(next != 0);
		CHECK_FALSE(buffer.matches(first));
		CHECK_FALSE(buffer.complete(upload, true, true));
		CHECK_FALSE(buffer.complete(next, false, false));
		CHECK_FALSE(buffer.idle());
		CHECK(buffer.begin(first) == 0);
		buffer.proveRetirement(next);
		CHECK(buffer.idle());
		const auto retry = buffer.begin(second);
		CHECK(buffer.complete(retry, true, true));
		CHECK(buffer.matches(second));
		CHECK_FALSE(buffer.matches(first));
		auto foreign = second;
		foreign.world = 18;
		CHECK_FALSE(buffer.matches(foreign));
		foreign = second;
		foreign.owner = std::make_shared<unsigned>(1);
		CHECK_FALSE(buffer.matches(foreign));
		foreign = second;
		foreign.tableVersion = 4;
		CHECK_FALSE(buffer.matches(foreign));
	}
	TEST_CASE("Transfer planning charges widened coalesced bytes and speculative limits")
	{
		using namespace gradient_kernel::residency;
		std::vector<std::uint64_t> old(16, 1), now = old;
		auto cold = planUploads(64, 64, 12, {}, now, 4, 64);
		CHECK(cold.full);
		CHECK(cold.bytes == 64 * 64 * 12 + 64);
		CHECK(cold.fitsSpeculation());
		now[0] = 2;
		auto sparse = planUploads(64, 64, 12, old, now);
		CHECK_FALSE(sparse.full);
		CHECK(sparse.ranges.size() == 16);
		CHECK(sparse.bytes == 16 * 16 * 12);
		now[2] = 2;
		CHECK_FALSE(planUploads(64, 64, 12, old, now).fitsSpeculation());
		now.assign(16, 2);
		CHECK(planUploads(64, 64, 12, old, now).full);
		std::vector<std::uint64_t> large(1024, 1);
		CHECK_FALSE(planUploads(512, 512, 12, {}, large).fitsSpeculation());
		CHECK(planUploads(64, 64, 12, old, old, 4, 0, true).full);
		CHECK(planUploads(64, 64, 12, old, old).ranges.empty());
		// A partial wrapped chunk still refers to the original rectangular array.
		CHECK(planUploads(18, 18, 2, {}, std::vector<std::uint64_t>(4, 1)).bytes == 18 * 18 * 2);
		CHECK_THROWS_AS(planUploads(1, 1, 2, {}, {}, 4), std::invalid_argument);
	}
	TEST_CASE("Residency invalidation retains owners until the final lease and transfer retire")
	{
		using namespace gradient_kernel::residency;
		static_assert(!std::is_copy_constructible_v<Buffer>);
		static_assert(!std::is_move_constructible_v<Buffer>);
		Buffer buffer;
		auto owner = std::make_shared<unsigned>(7);
		std::weak_ptr<const void> weak = owner;
		Identity identity{1, 1, 0, 16, 16, 0, owner, {}, {1}};
		auto invalid = identity;
		invalid.world = 0;
		CHECK(buffer.begin(invalid) == 0);
		invalid = identity;
		invalid.width = 0;
		CHECK(buffer.begin(invalid) == 0);
		invalid = identity;
		invalid.owner.reset();
		CHECK(buffer.begin(invalid) == 0);
		invalid = identity;
		invalid.tableVersion = 1;
		CHECK(buffer.begin(invalid) == 0);
		invalid = {};
		const auto serial = buffer.begin(identity);
		REQUIRE(serial != 0);
		CHECK_FALSE(buffer.complete(serial, true, false));
		CHECK(buffer.state() == State::Uploading);
		CHECK_FALSE(buffer.idle());
		CHECK(buffer.complete(serial, true, true));
		REQUIRE(buffer.acquire(identity));
		REQUIRE(buffer.acquire(identity));
		identity.owner.reset();
		owner.reset();
		invalid = {};
		buffer.invalidate();
		CHECK_FALSE(weak.expired());
		CHECK_FALSE(buffer.acquire(identity));
		CHECK_FALSE(buffer.idle());
		buffer.release();
		CHECK_FALSE(weak.expired());
		buffer.release();
		CHECK(weak.expired());
		CHECK(buffer.idle());
		CHECK_THROWS_AS(buffer.release(), std::logic_error);

		owner = std::make_shared<unsigned>(8);
		weak = owner;
		identity.owner = owner;
		const auto next = buffer.begin(identity);
		identity.owner.reset();
		owner.reset();
		buffer.invalidate();
		buffer.proveRetirement(serial); // An old fence cannot retire a newer upload.
		CHECK_FALSE(weak.expired());
		CHECK_FALSE(buffer.idle());
		buffer.proveRetirement(next);
		CHECK(weak.expired());
		CHECK(buffer.idle());
		CHECK_FALSE(buffer.complete(next, true, true));
	}
	TEST_CASE("Upload ranges cover changed cells without escaping the original owner")
	{
		using namespace gradient_kernel::residency;
		for (unsigned width : {1u, 16u, 18u, 32u, 65u})
			for (unsigned height : {1u, 17u, 32u})
				for (std::size_t stride : {4u, 8u, 12u})
				{
					const std::size_t count = ((width - 1) / 16 + 1) * ((height - 1) / 16 + 1);
					std::vector<std::uint64_t> before(count, 1), after = before;
					for (std::size_t dirty = 0; dirty < count; ++dirty)
					{
						after = before;
						after[dirty] = 2;
						const auto plan = planUploads(width, height, stride, before, after, 4, 13);
						std::size_t charged = 13, end = 0;
						for (const auto &range : plan.ranges)
						{
							CHECK(range.offset >= end);
							CHECK(range.bytes > 0);
							CHECK(range.offset % 4 == 0);
							CHECK(range.bytes % 4 == 0);
							end = range.offset + range.bytes;
							CHECK(end <= std::size_t(width) * height * stride);
							charged += range.bytes;
						}
						CHECK(plan.bytes == charged);
						for (unsigned y = 0; y < height; ++y)
							for (unsigned x = 0; x < width; ++x)
							{
								const auto chunk =
									std::size_t(y / 16) * ((width - 1) / 16 + 1) + x / 16;
								if (chunk != dirty)
									continue;
								const auto offset = (std::size_t(y) * width + x) * stride;
								bool covered = false;
								for (const auto &range : plan.ranges)
									covered |= offset >= range.offset &&
											   offset + stride <= range.offset + range.bytes;
								CHECK(covered);
							}
					}
				}
		const auto maximum = std::numeric_limits<std::size_t>::max();
		CHECK_THROWS_AS(planUploads(16, 16, maximum, {}, {}), std::invalid_argument);
		CHECK_THROWS_AS(planUploads(16, 16, 4, {}, {}, 4, maximum), std::invalid_argument);
		CHECK_THROWS_AS(planUploads(0, 16, 4, {}, {}), std::invalid_argument);
		CHECK_THROWS_AS(planUploads(16, 16, 0, {}, {}), std::invalid_argument);
		CHECK_THROWS_AS(planUploads(16, 16, 4, {}, {}, 3), std::invalid_argument);
		CHECK_THROWS_AS(planUploads(64, 64, 4, {}, std::vector<std::uint64_t>(15, 1)),
						std::invalid_argument);
		CHECK_THROWS_AS(planUploads(64, 64, 4, {}, std::vector<std::uint64_t>(17, 1)),
						std::invalid_argument);
		// Metadata consumes a copy region even when the data alone uses all 16.
		std::vector<std::uint64_t> before(16, 1), after = before;
		after[0] = 2;
		CHECK(planUploads(64, 64, 4, before, after).fitsSpeculation());
		CHECK_FALSE(planUploads(64, 64, 4, before, after, 4, 4).fitsSpeculation());
		CHECK(planUploads(256, 256, 4, {}, std::vector<std::uint64_t>(256, 1)).fitsSpeculation());
		CHECK_FALSE(planUploads(256, 256, 4, {}, std::vector<std::uint64_t>(256, 1), 4, 1)
						.fitsSpeculation());
	}
	TEST_CASE("Native records retain explicit decode layouts")
	{
		using namespace gradient_kernel::residency;
		CHECK(sizeof(Resource) == 8);
		CHECK(offsetof(Resource, type) == 0);
		CHECK(offsetof(Resource, amount) == 4);
		CHECK(sizeof(MapState::ResourceCell) == 12);
		CHECK(offsetof(MapState::ResourceCell, resource) == 0);
		CHECK(sizeof(MapState::OccupancyCell) == 8);
		CHECK(offsetof(MapState::OccupancyCell, immobileUnit) == 6);
		CHECK(sizeof(MapState::AreaCell) == 16);
		CHECK(offsetof(MapState::AreaCell, farm) == 12);
		CHECK(sizeof(GradientPreparationRule) == 36);
		CHECK(ResourceRegistry::Capacity == 16384);
		CHECK(supportsPreparationRecords());
		auto changed = nativeRecordRepresentation;
		changed.resourceStride += 4;
		CHECK_FALSE(supportsPreparationRecords(changed));
		changed = nativeRecordRepresentation;
		changed.littleEndian = false;
		CHECK_FALSE(supportsPreparationRecords(changed));
		changed = nativeRecordRepresentation;
		changed.immobileOffset = 7;
		CHECK_FALSE(supportsPreparationRecords(changed));
		// Every field participates in representation admission, not just strides.
		for (auto member :
			 {&RecordRepresentation::resourceStride, &RecordRepresentation::resourceOffset,
			  &RecordRepresentation::typeOffset, &RecordRepresentation::amountOffset,
			  &RecordRepresentation::occupancyStride, &RecordRepresentation::buildingOffset,
			  &RecordRepresentation::immobileOffset, &RecordRepresentation::areaStride,
			  &RecordRepresentation::forbiddenOffset, &RecordRepresentation::clearOffset,
			  &RecordRepresentation::farmOffset})
		{
			changed = nativeRecordRepresentation;
			++(changed.*member);
			CHECK_FALSE(supportsPreparationRecords(changed));
		}
		const std::array raw{std::byte{0xff}, std::byte{0x00}, std::byte{0x80}, std::byte{0x12},
							 std::byte{0x34}};
		CHECK(decodeLittleEndian(raw, 0, 1) == 255);
		CHECK(decodeLittleEndian(raw, 1, 2) == 0x8000);
		CHECK(decodeLittleEndian(raw, 1, 4) == 0x34128000);
		CHECK_FALSE(decodeLittleEndian({}, 0, 1).has_value());
		CHECK_FALSE(decodeLittleEndian(raw, raw.size(), 1).has_value());
		const MapState::ResourceCell cell{Resource{16383, 4, 0x12345678, 7}, 0, 0};
		const auto bytes = std::as_bytes(std::span(&cell, 1));
		CHECK(decodeLittleEndian(bytes, nativeRecordRepresentation.typeOffset, 2) == 16383);
		CHECK(decodeLittleEndian(bytes, nativeRecordRepresentation.amountOffset, 4) == 0x12345678);
		CHECK_FALSE(decodeLittleEndian(bytes, bytes.size() - 1, 4).has_value());
		CHECK_FALSE(decodeLittleEndian(bytes, std::size_t(-1), 1).has_value());
		CHECK_FALSE(decodeLittleEndian(bytes, 0, 3).has_value());
	}
}
