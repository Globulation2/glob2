// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <SDL.h>
#include <string>
#include <filesystem>
#include <cstdio>

#include <glob2/BuildConfig.h>
#include <Toolkit.h>
#include <zlib.h>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define glob2_dup _dup
#define glob2_dup2 _dup2
#define glob2_fileno _fileno
#define glob2_close _close
#else
#include <unistd.h>
#define glob2_dup dup
#define glob2_dup2 dup2
#define glob2_fileno fileno
#define glob2_close close
#endif

namespace glob2test
{
	namespace
	{
		std::string suiteName;
		std::string caseName;
		std::atomic<unsigned> uniqueCounter{0};

		std::string sanitized(const std::string& text)
		{
			std::string out;
			for (char c : text)
				out += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') ? c : '_';
			while (!out.empty() && out.back() == '_') out.pop_back();
			return out.empty() ? "unnamed" : out;
		}

		std::string normalised(std::string text)
		{
			std::string out;
			out.reserve(text.size());
			for (size_t i = 0; i < text.size(); ++i)
				if (text[i] != '\r' || i + 1 >= text.size() || text[i + 1] != '\n')
					out += text[i];
			return out;
		}

		// Records the running test so artifactDir() can name directories after it.
		struct CurrentTestListener : doctest::IReporter
		{
			explicit CurrentTestListener(const doctest::ContextOptions&) {}
			void report_query(const doctest::QueryData&) override {}
			void test_run_start() override {}
			void test_run_end(const doctest::TestRunStats&) override {}
			void test_case_start(const doctest::TestCaseData& data) override
			{
				suiteName = data.m_test_suite ? data.m_test_suite : "";
				caseName = data.m_name ? data.m_name : "";
			}
			void test_case_reenter(const doctest::TestCaseData&) override {}
			void test_case_end(const doctest::CurrentTestCaseStats&) override {}
			void test_case_exception(const doctest::TestCaseException&) override {}
			void subcase_start(const doctest::SubcaseSignature&) override {}
			void subcase_end() override {}
			void log_assert(const doctest::AssertData&) override {}
			void log_message(const doctest::MessageData&) override {}
			void test_case_skipped(const doctest::TestCaseData&) override {}
		};
		REGISTER_LISTENER("glob2-current-test", 1, CurrentTestListener);
	}

	std::filesystem::path sourceRoot()
	{
		// The build host's checkout, unless the binary runs somewhere else (an Android
		// device, say) and the runner staged data/, maps/, games/ and test/fixtures/.
		if (const char* env = std::getenv("GLOB2_TEST_SOURCE_ROOT"); env && *env)
			return std::filesystem::path(env);
		return std::filesystem::path(PACKAGE_SOURCE_DIR);
	}

	std::filesystem::path fixture(const std::string& relative)
	{
		return sourceRoot() / "test" / "fixtures" / relative;
	}

	std::filesystem::path profileDir()
	{
		// SDL owns this variable in TestMain and FileManager. On Windows its
		// environment can differ from the executable's CRT getenv cache.
		const char* dir = SDL_getenv("GLOB2_USER_DATA_DIR");
		GLOB2_REQUIRE(dir && *dir, "GLOB2_USER_DATA_DIR must name the disposable profile (TestMain sets it)");
		return std::filesystem::path(dir);
	}

	std::filesystem::path artifactDir()
	{
		std::filesystem::path dir;
		const std::filesystem::path perCase = std::filesystem::path(sanitized(suiteName.empty() ? "no-suite" : suiteName)) / sanitized(caseName);
		if (const char* env = std::getenv("GLOB2_TEST_ARTIFACTS"); env && *env)
			dir = env;
		else if (const char* root = std::getenv("GLOB2_TEST_ARTIFACTS_ROOT"); root && *root)
			dir = std::filesystem::path(root) / perCase;
		else
			dir = sourceRoot() / "artifacts" / "tests" / perCase;
		std::filesystem::create_directories(dir);
		return dir;
	}

	std::string artifactDirFromWorkingDirectory()
	{
		const auto dir = artifactDir();
		std::error_code ignored;
		const auto relative = std::filesystem::relative(dir, std::filesystem::current_path(), ignored);
		return (relative.empty() ? dir : relative).string();
	}

	TempDir::TempDir(const std::string& prefix)
	{
		path = profileDir() / (prefix + "-" + std::to_string(uniqueCounter++));
		std::filesystem::remove_all(path);
		std::filesystem::create_directories(path);
	}

	TempDir::~TempDir()
	{
		std::error_code ignored;
		std::filesystem::remove_all(path, ignored);
	}

	CapturedStream::CapturedStream(FILE* stream) : stream(stream)
	{
		std::fflush(stream);
		file = profileDir() / ("captured-" + std::to_string(uniqueCounter++) + ".txt");
		savedDescriptor = glob2_dup(glob2_fileno(stream));
		FILE* target = std::fopen(file.string().c_str(), "w");
		REQUIRE_MESSAGE(target != nullptr, "cannot open capture file " << file.string());
		glob2_dup2(glob2_fileno(target), glob2_fileno(stream));
		std::fclose(target);
	}

	CapturedStream::~CapturedStream()
	{
		std::fflush(stream);
		glob2_dup2(savedDescriptor, glob2_fileno(stream));
		glob2_close(savedDescriptor);
		std::error_code ignored;
		std::filesystem::remove(file, ignored);
	}

	std::string CapturedStream::text()
	{
		std::fflush(stream);
		return readFile(file);
	}

	bool updatingFixtures()
	{
		const char* value = std::getenv("GLOB2_TEST_UPDATE_FIXTURES");
		return value && *value && std::string(value) != "0";
	}

	bool fullscreenEnabled()
	{
		const char* value = std::getenv("GLOB2_TEST_FULLSCREEN");
		return value && std::string(value) == "1";
	}

	void expectGolden(const std::string& relative, const std::string& actual)
	{
		const std::filesystem::path path = fixture(relative);
		const std::string text = normalised(actual);
		if (updatingFixtures())
		{
			std::filesystem::create_directories(path.parent_path());
			writeFile(path, text);
			MESSAGE("updated golden fixture " << path.string());
			return;
		}
		REQUIRE_MESSAGE(std::filesystem::exists(path), "missing golden fixture " << path.string() << " (run with --update-fixtures to create it)");
		const std::string expected = normalised(readFile(path));
		if (expected == text)
			return;
		std::istringstream expectedLines(expected), actualLines(text);
		std::string e, a;
		int line = 0;
		while (true)
		{
			const bool haveExpected = static_cast<bool>(std::getline(expectedLines, e));
			const bool haveActual = static_cast<bool>(std::getline(actualLines, a));
			++line;
			if (!haveExpected && !haveActual) break;
			if (!haveExpected || !haveActual || e != a)
			{
				FAIL("golden mismatch against " << relative << " at line " << line
				     << "\n  expected: " << (haveExpected ? e : std::string("<end of file>"))
				     << "\n  actual:   " << (haveActual ? a : std::string("<end of file>")));
				return;
			}
		}
		FAIL("golden mismatch against " << relative << " (whitespace-only difference)");
	}

	std::filesystem::path inflated(const std::string& relativeGz)
	{
		std::filesystem::path source = relativeGz;
		if (source.is_relative() && !std::filesystem::exists(source))
			source = std::filesystem::exists(fixture(relativeGz)) ? fixture(relativeGz) : sourceRoot() / relativeGz;
		REQUIRE_MESSAGE(std::filesystem::exists(source), "missing compressed fixture " << source.string());
		std::filesystem::path target = profileDir() / "inflated" / source.filename();
		if (target.extension() == ".gz") target.replace_extension();
		std::filesystem::create_directories(target.parent_path());
		gzFile in = gzopen(source.string().c_str(), "rb");
		REQUIRE_MESSAGE(in != nullptr, "cannot read " << source.string());
		std::ofstream out(target, std::ios::binary);
		std::vector<char> buffer(1 << 16);
		for (int read; (read = gzread(in, buffer.data(), static_cast<unsigned>(buffer.size()))) > 0;)
			out.write(buffer.data(), read);
		gzclose(in);
		return target;
	}

	std::string readFile(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		std::ostringstream text;
		text << in.rdbuf();
		return text.str();
	}

	void writeFile(const std::filesystem::path& path, const std::string& text)
	{
		std::ofstream out(path, std::ios::binary);
		out << text;
	}

	void setEnv(const char* name, const char* value)
	{
#ifdef _WIN32
		_putenv_s(name, value);
#else
		setenv(name, value, 1);
#endif
	}

	void unsetEnv(const char* name)
	{
#ifdef _WIN32
		_putenv_s(name, ""); // An empty value removes the variable on Windows.
#else
		unsetenv(name);
#endif
	}

	int retainFromProfile(const std::string& extension)
	{
		int copied = 0;
		const std::filesystem::path target = artifactDir();
		for (const auto& entry : std::filesystem::recursive_directory_iterator(profileDir()))
			if (entry.is_regular_file() && entry.path().extension() == extension)
			{
				std::error_code ignored;
				std::filesystem::copy_file(entry.path(), target / entry.path().filename(), std::filesystem::copy_options::overwrite_existing, ignored);
				++copied;
			}
		return copied;
	}

	ToolkitScope::ToolkitScope(const char* profileName)
	{
		GAGCore::Toolkit::init(profileName);
	}

	ToolkitScope::~ToolkitScope()
	{
		GAGCore::Toolkit::close();
	}

	const std::string& currentTestName() { return caseName; }
	const std::string& currentTestSuite() { return suiteName; }
}
