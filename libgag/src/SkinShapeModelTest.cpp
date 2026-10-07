// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <SkinMesh.h>
#include <SkinShapeModel.h>
#include <StreamBackend.h>
#include <nlohmann/json.hpp>
#include <cmath>
namespace
{
auto fixture()
{
	return nlohmann::json::parse(
		glob2test::readFile(glob2test::sourceRoot() / "test/fixtures/skins/shapes.json"));
}
std::vector<std::uint8_t> bytes(const std::string &hex)
{
	std::vector<std::uint8_t> result;
	for (std::size_t i = 0; i < hex.size(); i += 2)
		result.push_back(std::stoul(hex.substr(i, 2), nullptr, 16));
	return result;
}
} // namespace
TEST_SUITE("SkinShapeModel")
{
	TEST_CASE("shared fixture decodes and evaluates the analytic frames")
	{
		const auto data = fixture();
		const auto encoded = bytes(data["hex"].get<std::string>());
		std::string error;
		const auto model = GAGCore::SkinShapeModel::decode(encoded, error);
		REQUIRE_MESSAGE(model, error);
		CHECK(error.empty());
		CHECK(model->vertices() == data["header"]["vertices"].get<unsigned>());
		CHECK(model->shapes() == data["header"]["shapes"].get<unsigned>());
		CHECK(model->normalShapes() == data["header"]["normalShapes"].get<unsigned>());
		CHECK(model->clips().size() == data["header"]["clips"].get<unsigned>());
		CHECK(model->logicalSize() == data["header"]["logicalSize"].get<unsigned>());
		CHECK(model->identity() != 0);
		std::vector<float> output;
		for (const auto &[key, expected] : data["expected"].items())
		{
			const auto clip = std::stoul(key.substr(0, key.find(':')));
			const auto frame = std::stoul(key.substr(key.find(':') + 1));
			REQUIRE(model->evaluate(clip, frame, output));
			REQUIRE(output.size() == expected.size() * 6);
			for (unsigned v = 0; v < expected.size(); ++v)
				for (unsigned k = 0; k < 6; ++k)
				{
					INFO(key << " vertex " << v << " component " << k);
					CHECK(std::abs(output[v * 6 + k] - expected[v][k].get<float>()) < 0.00001f);
				}
		}
		const auto pointer = output.data();
		REQUIRE(model->evaluate(0, 0, output));
		CHECK(output.data() == pointer);
		const auto before = output;
		CHECK_FALSE(model->evaluate(0, 256, output));
		CHECK(output == before);
		CHECK_FALSE(model->evaluate(2, 0, output));
		CHECK(output == before);
		// Every frame of every clip stays finite with unit normals.
		for (unsigned clip = 0; clip < model->clips().size(); ++clip)
			for (unsigned frame = 0; frame < 256; ++frame)
			{
				REQUIRE(model->evaluate(clip, frame, output));
				for (unsigned v = 0; v < model->vertices(); ++v)
				{
					const float *n = output.data() + v * 6 + 3;
					CHECK(std::abs(n[0] * n[0] + n[1] * n[1] + n[2] * n[2] - 1) < 0.0001f);
					for (unsigned k = 0; k < 6; ++k)
						CHECK(std::isfinite(output[v * 6 + k]));
				}
			}
		// Model space differs from clip space by the camera.
		std::vector<float> modelSpace;
		REQUIRE(model->evaluate(0, 0, modelSpace, false));
		REQUIRE(model->evaluate(0, 0, output, true));
		CHECK(modelSpace.size() == output.size());
		CHECK(modelSpace != output);
	}
	TEST_CASE("malformed assets never publish partial models")
	{
		const auto data = fixture();
		for (const auto &[name, hex] : data["malformed"].items())
		{
			std::string error;
			INFO(name);
			CHECK_FALSE(GAGCore::SkinShapeModel::decode(bytes(hex.get<std::string>()), error));
			CHECK_FALSE(error.empty());
		}
	}
	TEST_CASE("skin mesh adapter loads shape clips transactionally")
	{
		const auto data = fixture();
		const auto encoded = bytes(data["hex"].get<std::string>());
		GAGCore::MemoryStreamBackend stream(encoded.data(), encoded.size());
		GAGCore::SkinMesh mesh;
		std::string error;
		REQUIRE_MESSAGE(mesh.load(stream, error), error);
		CHECK(mesh.shapes);
		CHECK_FALSE(mesh.model);
		CHECK(mesh.frames == 256);
		CHECK(mesh.vertices == data["header"]["vertices"].get<unsigned>());
		CHECK(mesh.logicalSize == data["header"]["logicalSize"].get<unsigned>());
		CHECK(mesh.poses.empty());
		CHECK(mesh.uv.size() == mesh.vertices * 2);
		CHECK(mesh.indices.size() == data["header"]["indices"].get<unsigned>());
		std::vector<float> output, direct;
		REQUIRE(mesh.evaluate(5, output));
		REQUIRE(mesh.shapes->evaluate(0, 5, direct));
		CHECK(output == direct);
		CHECK_FALSE(mesh.evaluate(256, output));
		const auto second = GAGCore::SkinMesh::fromShapes(mesh.shapes, 1);
		CHECK(second.identity != mesh.identity);
		CHECK(second.clip == 1);
		CHECK_FALSE(GAGCore::SkinMesh::fromShapes(mesh.shapes, 2).identity);
		// A broken stream leaves the existing mesh untouched.
		const auto broken = bytes(data["malformed"]["truncated"].get<std::string>());
		GAGCore::MemoryStreamBackend brokenStream(broken.data(), broken.size());
		const auto identity = mesh.identity;
		CHECK_FALSE(mesh.load(brokenStream, error));
		CHECK(mesh.identity == identity);
		CHECK(mesh.shapes);
	}
}
