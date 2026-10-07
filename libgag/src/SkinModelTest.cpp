// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <SkinModel.h>
#include "src/unit/render/UnitAnimation.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
namespace
{
auto fixture()
{
	return nlohmann::json::parse(
		glob2test::readFile(glob2test::sourceRoot() / "test/fixtures/skins/rig.json"));
}
std::vector<std::uint8_t> bytes(const nlohmann::json &fixture)
{
	const auto hex = fixture["hex"].get<std::string>();
	std::vector<std::uint8_t> result;
	for (std::size_t i = 0; i < hex.size(); i += 2)
		result.push_back(std::stoul(hex.substr(i, 2), nullptr, 16));
	return result;
}
} // namespace
TEST_SUITE("SkinModel")
{
	TEST_CASE("shared fixture covers hierarchy weights normals interpolation and all legacy frames")
	{
		const auto data = fixture();
		const auto encoded = bytes(data);
		std::string error;
		const auto model = GAGCore::SkinModel::decode(encoded, error);
		REQUIRE_MESSAGE(model, error);
		CHECK(error.empty());
		CHECK(model->vertices() == 3);
		std::vector<float> output;
		for (unsigned clip = 0; clip < 2; ++clip)
			for (unsigned frame = 0; frame < 256; ++frame)
			{
				REQUIRE(model->evaluate(clip, frame, output));
				for (unsigned i = 0; i < output.size(); ++i)
				{
					INFO(clip << " " << frame << " " << i);
					CHECK(std::abs(output[i] - data["expected"][frame][i].get<float>()) < 0.00001f);
				}
			}
		const auto pointer = output.data();
		REQUIRE(model->evaluate(0, 0, output));
		CHECK(output.data() == pointer);
		const auto beforeInvalidRequest = output;
		CHECK_FALSE(model->evaluate(0, 256, output));
		CHECK(output == beforeInvalidRequest);
		CHECK_FALSE(model->evaluate(2, 0, output));
		CHECK(output == beforeInvalidRequest);
		// Direction 8 chooses the first phase of successive headings; ordinary
		// directions preserve all 32 samples, including both gait-half boundaries.
		for (unsigned direction = 0; direction <= 8; ++direction)
			for (unsigned delta = 0; delta < 256; ++delta)
			{
				const auto frame = unitAnimationFrame(0, direction, delta);
				REQUIRE(frame >= 0);
				REQUIRE(frame < 256);
				const auto &mapping = model->clips()[0].frames[frame];
				CHECK(std::abs(mapping.heading + (frame / 32) * 3.14159265358979323846f / 4) <
					  0.000001f);
				CHECK(mapping.time == float(((frame / 32) % 2) * 32 + frame % 32) / 32);
			}
		GAGCore::SkinPalette a, b;
		for (double time : {0., .125, .5, 1., 1.5, 1.999})
		{
			REQUIRE(model->palette(0, time, 0, a));
			REQUIRE(model->palette(0, time + 2, 0, b));
			for (unsigned i = 0; i < 2; ++i)
				for (unsigned k = 0; k < 16; ++k)
					CHECK(std::abs(a.positions[i][k] - b.positions[i][k]) < 0.00001f);
			REQUIRE(model->palette(0, time - 2, 0, b));
			for (unsigned i = 0; i < 2; ++i)
				for (unsigned k = 0; k < 16; ++k)
					CHECK(std::abs(a.positions[i][k] - b.positions[i][k]) < 0.00001f);
		}
		const auto beforeInvalidPalette = b;
		CHECK_FALSE(model->palette(0, std::numeric_limits<double>::infinity(), 0, b));
		CHECK_FALSE(model->palette(0, 0, std::numeric_limits<float>::quiet_NaN(), b));
		CHECK_FALSE(model->paletteForFrame(0, 256, b));
		CHECK(b.count == beforeInvalidPalette.count);
		CHECK(b.positions == beforeInvalidPalette.positions);
		CHECK(b.normals == beforeInvalidPalette.normals);
		REQUIRE(model->evaluate(0, 0, output, false));
		for (unsigned i = 0; i < output.size(); ++i)
			CHECK(std::abs(output[i] - model->rest()[i]) < 0.000001f);
		const auto again = GAGCore::SkinModel::decode(encoded, error);
		REQUIRE(again);
		CHECK(model->identity() != again->identity());
	}
	TEST_CASE("affine camera translation is independent of rounded influence sums")
	{
		const auto data = fixture()["affineCamera"];
		std::string error;
		const auto model = GAGCore::SkinModel::decode(bytes(data), error);
		REQUIRE_MESSAGE(model, error);
		float roundedSum = 0;
		for (const auto weight : model->influences()[0].weights)
			roundedSum += weight;
		REQUIRE(roundedSum != 1.f);
		std::vector<float> output;
		REQUIRE(model->evaluate(0, 0, output));
		for (unsigned i = 0; i < output.size(); ++i)
			CHECK(std::abs(output[i] - data["expected"][i].get<float>()) < 0.00001f);
	}
	TEST_CASE("serialized float boundaries have identical native and Studio acceptance")
	{
		const auto data = fixture();
		const auto encoded = bytes(data);
		for (const auto &boundary : data["boundaries"])
		{
			auto candidate = encoded;
			for (const auto &patch : boundary["patches"])
			{
				const unsigned at = patch["offset"], word = patch["word"];
				for (unsigned i = 0; i < 4; ++i)
					candidate[at + i] = word >> (8 * i);
			}
			std::string error;
			const auto model = GAGCore::SkinModel::decode(candidate, error);
			INFO(boundary["name"] << ": " << error);
			CHECK(bool(model) == boundary["accepted"].get<bool>());
			CHECK(error.empty() == bool(model));
		}
	}
	TEST_CASE("malformed assets never publish partial models")
	{
		const auto data = fixture();
		const auto encoded = bytes(data);
		std::string error;
		for (const auto &corruption : data["malformed"])
		{
			auto bad = encoded;
			const unsigned at = corruption["offset"], word = corruption["word"];
			for (unsigned i = 0; i < 4; ++i)
				bad[at + i] = word >> (8 * i);
			INFO(corruption["name"]);
			CHECK_FALSE(GAGCore::SkinModel::decode(bad, error));
			CHECK_FALSE(error.empty());
		}
		for (unsigned end : {0u, 27u, 28u, 220u, unsigned(encoded.size() - 1)})
		{
			CHECK_FALSE(GAGCore::SkinModel::decode(std::span(encoded).first(end), error));
		}
		auto extra = encoded;
		extra.push_back(0);
		CHECK_FALSE(GAGCore::SkinModel::decode(extra, error));
	}
}
