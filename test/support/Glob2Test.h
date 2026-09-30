// SPDX-License-Identifier: GPL-3.0-or-later
// The one header every test includes: doctest with the project configuration, plus
// helpers that need neither the engine nor a GlobalContainer. Engine tests include
// EngineFixtures.h instead, which includes this.
#pragma once

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include "../third_party/doctest.h"

#include <cstdio>
#include <filesystem>
#include <string>

// Tags are bracketed words at the end of a test-case name, e.g.
// GLOB2_TEST_CASE("renders every zoom level", "[display][artifacts]"). test/run_tests.py
// reads them to decide isolation, display, timeout and artifact handling; doctest's
// own -tc='*[display]*' filter selects them on the command line.
#define GLOB2_TEST_CASE(name, tags) TEST_CASE(name " " tags)

// Boolean assertions with a message, for conditions that doctest cannot decompose
// (anything with && or ||) and for migrated require(cond, "message") calls. Prefer
// REQUIRE/CHECK with a single binary comparison when the operands are worth printing.
#define GLOB2_REQUIRE(cond, ...) REQUIRE_MESSAGE(static_cast<bool>(cond), __VA_ARGS__)
#define GLOB2_CHECK(cond, ...) CHECK_MESSAGE(static_cast<bool>(cond), __VA_ARGS__)

namespace glob2test
{
	// Repository root: PACKAGE_SOURCE_DIR from the generated BuildConfig.h.
	std::filesystem::path sourceRoot();

	// test/fixtures/<relative>.
	std::filesystem::path fixture(const std::string& relative);

	// The disposable profile the process runs in (GLOB2_USER_DATA_DIR). TestMain
	// guarantees it is set; the runner sets a fresh one per test case.
	std::filesystem::path profileDir();

	// Where a test leaves files a reviewer can download: GLOB2_TEST_ARTIFACTS when the
	// runner sets it, otherwise artifacts/tests/<suite>/<case> under the source root.
	// Created on first use.
	std::filesystem::path artifactDir();

	// A fresh empty directory under the profile, removed on destruction.
	struct TempDir
	{
		std::filesystem::path path;
		explicit TempDir(const std::string& prefix = "scratch");
		~TempDir();
		TempDir(const TempDir&) = delete;
		TempDir& operator=(const TempDir&) = delete;
	};

	// Redirects a C stream to a file for the object's lifetime; text() returns what was
	// written so far. Replaces the freopen() tricks in the old harnesses.
	class CapturedStream
	{
	public:
		explicit CapturedStream(FILE* stream);
		~CapturedStream();
		std::string text();
		CapturedStream(const CapturedStream&) = delete;
		CapturedStream& operator=(const CapturedStream&) = delete;
	private:
		FILE* stream;
		int savedDescriptor;
		std::filesystem::path file;
	};
	struct CapturedStdout : CapturedStream { CapturedStdout() : CapturedStream(stdout) {} };
	struct CapturedStderr : CapturedStream { CapturedStderr() : CapturedStream(stderr) {} };

	// GLOB2_TEST_UPDATE_FIXTURES=1 (run_tests.py --update-fixtures): golden comparisons
	// rewrite their expected text instead of checking it.
	bool updatingFixtures();

	// Compares `actual` with test/fixtures/<relative>, line endings normalised, and
	// reports the first differing line. Rewrites the fixture when updatingFixtures().
	void expectGolden(const std::string& relative, const std::string& actual);

	// Decompresses test/fixtures/<relative>.gz (or any repository-relative .gz) into
	// the profile and returns the raw file, for loaders that want an uncompressed path.
	std::filesystem::path inflated(const std::string& relativeGz);

	std::string readFile(const std::filesystem::path& path);
	void writeFile(const std::filesystem::path& path, const std::string& text);

	// GAGCore::Toolkit::init/close for one test case, for tests that need a FileManager
	// (it opens the disposable profile) without an engine GlobalContainer.
	struct ToolkitScope
	{
		explicit ToolkitScope(const char* profileName = "glob2-tests");
		~ToolkitScope();
		ToolkitScope(const ToolkitScope&) = delete;
		ToolkitScope& operator=(const ToolkitScope&) = delete;
	};

	// Name of the running test case and suite, as doctest sees them.
	const std::string& currentTestName();
	const std::string& currentTestSuite();
}
