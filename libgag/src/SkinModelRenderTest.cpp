// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <SkinMesh.h>
#include <SkinModel.h>
#include <Toolkit.h>
#include <SDLGraphicContext.h>
#include <StreamBackend.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif
TEST_SUITE("SkinModelRender")
{
#ifdef HAVE_OPENGL
	TEST_CASE(
		"rig shader matches CPU poses for every heading and restores context resources [display]")
	{
		glob2test::ToolkitScope toolkit;
		auto *gfx = GAGCore::Toolkit::initGraphic(320, 240, GAGCore::GraphicContext::USEGPU,
												  "GSR1 agreement");
		const auto fixture = nlohmann::json::parse(
			glob2test::readFile(glob2test::sourceRoot() / "test/fixtures/skins/rig.json"));
		const auto hex = fixture["hex"].get<std::string>();
		std::vector<std::uint8_t> bytes;
		for (std::size_t i = 0; i < hex.size(); i += 2)
			bytes.push_back(std::stoul(hex.substr(i, 2), nullptr, 16));
		std::string error;
		const auto model = GAGCore::SkinModel::decode(bytes, error);
		REQUIRE_MESSAGE(model, error);
		auto rig = GAGCore::SkinMesh::fromModel(model, 0);
		auto baked = GAGCore::SkinMesh::fromModel(model, 0);
		baked.model.reset();
		std::vector<float> pose;
		for (unsigned frame = 0; frame < 256; ++frame)
		{
			REQUIRE(model->evaluate(0, frame, pose));
			baked.poses.insert(baked.poses.end(), pose.begin(), pose.end());
		}
		GAGCore::DrawableSurface paint(512, 512), material(512, 512);
		paint.drawFilledRect(0, 0, 512, 512, GAGCore::Color(220, 160, 80));
		material.drawFilledRect(0, 0, 512, 512, GAGCore::Color(1, 1, 1));
		std::vector<std::uint8_t> gpu, cpu;
		unsigned visibleFrames = 0;
		REQUIRE(gfx->readSkinMesh({&baked, 0, &paint, &material, 0}, cpu));
		CHECK(gfx->skinResources.rigProgram == 0);
		CHECK_FALSE(gfx->skinResources.rigAttempted);
		for (unsigned frame = 0; frame < 256; ++frame)
		{
			REQUIRE(gfx->readSkinMesh({&rig, frame, &paint, &material, 0}, gpu));
			REQUIRE(gfx->skinResources.rigProgram !=
					0); // Do not silently test the CPU fallback twice.
			REQUIRE(gfx->readSkinMesh({&baked, frame, &paint, &material, 0}, cpu));
			unsigned differences = 0, maximum = 0, coverage = 0;
			for (unsigned i = 0; i < gpu.size(); ++i)
			{
				const auto delta = unsigned(std::abs(int(gpu[i]) - int(cpu[i])));
				differences += delta != 0;
				maximum = std::max(maximum, delta);
				if (i % 4 == 3)
					coverage += gpu[i] != 0;
			}
			INFO("frame " << frame << " differences " << differences << " maximum " << maximum);
			CHECK(maximum <= 2);
			CHECK(differences <= 64);
			// The analytic triangle becomes subpixel near a few weighted folds.
			visibleFrames += coverage > 0;
		}
		CHECK(visibleFrames >= 200);
		// Fur displacement must follow the deformed camera-space normal on the
		// GPU, including when alternating with CPU-evaluated geometry.
		for (unsigned id : {3u, 18u})
		{
			material.drawFilledRect(0, 0, 512, 512, GAGCore::Color(id, id, id));
			for (unsigned frame : {0u, 37u, 91u, 193u})
			{
				REQUIRE(gfx->readSkinMesh({&rig, frame, &paint, &material, 0}, gpu));
				REQUIRE(gfx->skinResources.rigProgram != 0);
				REQUIRE(gfx->readSkinMesh({&baked, frame, &paint, &material, 0}, cpu));
				unsigned alphaDifferences = 0, totalDifference = 0;
				for (unsigned i = 0; i < gpu.size(); ++i)
				{
					totalDifference += unsigned(std::abs(int(gpu[i]) - int(cpu[i])));
					if (i % 4 == 3) alphaDifferences += gpu[i] != cpu[i];
				}
				INFO("fur material " << id << " frame " << frame);
				CHECK(alphaDifferences <= 8);
				CHECK(totalDifference <= 4096);
			}
		}
		material.drawFilledRect(0, 0, 512, 512, GAGCore::Color(1, 1, 1));
		// Mixing baked draws and team paints must retain the rig palette while
		// preserving the caller's texture units and compatibility client arrays.
		GAGCore::DrawableSurface secondPaint(512, 512);
		secondPaint.drawFilledRect(0, 0, 512, 512, GAGCore::Color(80, 160, 220));
		const GAGCore::SkinMeshRequest requests[] = {
			{&rig, 37, &paint, &material, 0},
			{&baked, 91, &secondPaint, &material, 0},
			{&rig, 37, &secondPaint, &material, 0},
		};
		std::vector<std::uint8_t> expected[3];
		for (unsigned i = 0; i < 3; ++i)
			REQUIRE(gfx->readSkinMesh(requests[i], expected[i]));
		gfx->skinResources.slots = {};
#ifndef GLOB2_WEBGL2
		glPushAttrib(GL_TEXTURE_BIT);
		glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
		GLuint textures[2];
		glGenTextures(2, textures);
		const GLfloat coordinates[] = {0, 0, 0, 1};
		for (unsigned unit = 1; unit <= 2; ++unit)
		{
			glActiveTexture(GL_TEXTURE0 + unit);
			glBindTexture(GL_TEXTURE_2D, textures[unit - 1]);
			glClientActiveTexture(GL_TEXTURE0 + unit);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			glTexCoordPointer(4, GL_FLOAT, 0, coordinates);
		}
#endif
		// Separate batches also exercise palette reuse after the baked program
		// has run, rather than only adjacent paint variants within one batch.
		gfx->prepareSkinMeshes({requests[0], requests[1]});
		gfx->prepareSkinMeshes({requests[2]});
#ifndef GLOB2_WEBGL2
		GLint active = 0;
		glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
		CHECK(active == GL_TEXTURE2);
		glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &active);
		CHECK(active == GL_TEXTURE2);
		for (unsigned unit = 1; unit <= 2; ++unit)
		{
			glActiveTexture(GL_TEXTURE0 + unit);
			GLint texture = 0;
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
			CHECK(texture == GLint(textures[unit - 1]));
			glClientActiveTexture(GL_TEXTURE0 + unit);
			CHECK(glIsEnabled(GL_TEXTURE_COORD_ARRAY));
			void *pointer = nullptr;
			glGetPointerv(GL_TEXTURE_COORD_ARRAY_POINTER, &pointer);
			CHECK(pointer == coordinates);
		}
		glPopClientAttrib();
		glPopAttrib();
		glDeleteTextures(2, textures);
#endif
		for (unsigned i = 0; i < 3; ++i)
		{
			REQUIRE(gfx->readSkinMesh(requests[i], gpu));
			CHECK(gpu == expected[i]);
		}
		REQUIRE(gfx->skinResources.rigs.size() == 1);
		REQUIRE(gfx->readSkinMesh({&rig, 37, &paint, &material, 0}, gpu));
		gfx->destroySkinRenderer();
		CHECK(gfx->skinResources.rigs.empty());
		REQUIRE(gfx->readSkinMesh({&rig, 37, &paint, &material, 0}, cpu));
		CHECK(gfx->skinResources.rigs.size() == 1);
		CHECK(cpu == gpu);
		// Force the capability fallback after resource creation. This exercises the
		// reusable CPU upload without changing process-wide environment variables.
		const auto program = gfx->skinResources.rigProgram;
		gfx->skinResources.rigProgram = 0;
		gfx->skinResources.slots = {};
		REQUIRE(gfx->readSkinMesh({&rig, 91, &paint, &material, 0}, gpu));
		REQUIRE(gfx->readSkinMesh({&baked, 91, &paint, &material, 0}, cpu));
		CHECK(cpu == gpu);
		CHECK(gfx->skinResources.cpuPose.size() == model->rest().size());
		gfx->skinResources.rigProgram = program;
	}
#endif
	TEST_CASE("rig stream loading is transactional and retains fixed geometry")
	{
		const auto fixture = nlohmann::json::parse(
			glob2test::readFile(glob2test::sourceRoot() / "test/fixtures/skins/rig.json"));
		const auto hex = fixture["hex"].get<std::string>();
		std::string bytes;
		for (std::size_t i = 0; i < hex.size(); i += 2)
			bytes.push_back(std::stoul(hex.substr(i, 2), nullptr, 16));
		GAGCore::MemoryStreamBackend stream(bytes.data(), bytes.size());
		GAGCore::SkinMesh mesh;
		std::string error;
		REQUIRE(mesh.load(stream, error));
		REQUIRE(mesh.model);
		CHECK(mesh.poses.empty());
		const auto identity = mesh.identity;
		const auto original = mesh.model;
		bytes.pop_back();
		GAGCore::MemoryStreamBackend truncated(bytes.data(), bytes.size());
		CHECK_FALSE(mesh.load(truncated, error));
		CHECK(mesh.identity == identity);
		CHECK(mesh.model == original);
		auto other = GAGCore::SkinMesh::fromModel(original, 1);
		CHECK(other.model == original);
		CHECK(other.identity != identity);
		CHECK(other.poses.empty());
		CHECK_FALSE(GAGCore::SkinMesh::fromModel(nullptr, 0).identity);
		CHECK_FALSE(GAGCore::SkinMesh::fromModel(original, original->clips().size()).identity);
		std::vector<float> actual, expected;
		REQUIRE(other.evaluate(255, actual));
		REQUIRE(original->evaluate(1, 255, expected));
		CHECK(actual == expected);
		CHECK_FALSE(other.evaluate(256, actual));
		CHECK(actual == expected);

		// A damaged public adapter must not walk past its baked pose storage.
		other.model.reset();
		CHECK_FALSE(other.evaluate(0, actual));
		CHECK(actual == expected);
	}
}
