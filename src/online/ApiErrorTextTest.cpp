// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ApiErrorText.h"
#include "ui/PresentedError.h"
#include "generated/OnlineErrorCatalog.h"
using namespace Online;
namespace {
const std::string Generic = "Нещо се обърка. Моля, опитайте отново.";
}
TEST_SUITE("ApiErrorText")
{
	TEST_CASE("Thai errors select reviewed Thai text and preserve literal parameters and diagnostics")
	{
		const auto error = errorFromJson(Json{{"code", "unauthenticated"}, {"message", "private diagnostic"},
			{"messageKey", "Sign in first (session.authenticate)."}}, "fallback");
		for (const auto *locale : {"th", "th-TH"})
		{
			CHECK_EQ(presentApiError(error, locale), "ลงชื่อเข้าใช้ก่อน ( session.authenticate )");
			CHECK_EQ(presentOnlineProblem("private driver error", locale), "เกิดข้อผิดพลาดบางอย่าง โปรดลองใหม่อีกครั้ง");
			CHECK_EQ(presentOnlineProblem("Building", locale), "เกิดข้อผิดพลาดบางอย่าง โปรดลองใหม่อีกครั้ง");
			CHECK_EQ(Glob2UI::presentException(std::runtime_error("private socket error"), locale,
				"เกิดข้อผิดพลาดบางอย่าง โปรดลองใหม่อีกครั้ง"), "เกิดข้อผิดพลาดบางอย่าง โปรดลองใหม่อีกครั้ง");
			const Glob2UI::PresentedError shown("ข้อความภาษาไทย", "private socket error");
			CHECK_EQ(Glob2UI::presentException(shown, locale, "unused summary"), "ข้อความภาษาไทย");
			auto contextual = error;
			contextual.messageKey = "Building (music mood)";
			CHECK_EQ(presentApiError(contextual, locale), "การก่อสร้าง");
			contextual.messageKey = "Building (generation progress)";
			CHECK_EQ(presentApiError(contextual, locale), "กำลังสร้าง");
			auto parameterized = errorFromJson(Json{{"message", "raw"}, {"messageKey", "{slot0} · {slot1}"},
				{"messageParams", {{"slot0", "ผู้เล่น {slot1} %0"}, {"slot1", 42}}}}, "fallback");
			CHECK_EQ(presentApiError(parameterized, locale), "ผู้เล่น {slot1} %0 · 42");
		}
		CHECK_EQ(error.message, "private diagnostic");
		CHECK_EQ(error.code, "unauthenticated");
		CHECK_EQ(presentApiError(error, "en"), error.message);
	}
	TEST_CASE("known metadata translates without rewriting diagnostics or details")
	{
		const Json body = {{"code", "unauthenticated"}, {"message", "raw diagnostic"},
			{"details", {{"raw", 17}}}, {"messageKey", "Sign in first (session.authenticate)."}};
		const auto error = errorFromJson(body, "fallback");
		CHECK_EQ(presentApiError(error, "bg"), "Първо влезте (session.authenticate).");
		CHECK_EQ(presentApiError(error, "bg-BG"), presentApiError(error, "bg"));
		CHECK_EQ(error.code, "unauthenticated");
		CHECK_EQ(error.message, "raw diagnostic");
		CHECK_EQ(error.details, body["details"]);
		CHECK_EQ(error.messageKey.value(), body["messageKey"].get<std::string>());
		CHECK(error.messageParams.empty());
		CHECK_FALSE(error.invalidLocalization);
		for (const auto *locale : {"en", "fr", "ru", "hi", ""})
			CHECK_EQ(presentApiError(error, locale), error.message);
	}
	TEST_CASE("exact literal fallback never guesses a translated or parameterized diagnostic")
	{
		ApiError error{"network", "Sign in first (session.authenticate).", Json()};
		CHECK_EQ(presentApiError(error, "bg"), "Първо влезте (session.authenticate).");
		error.message += " ";
		CHECK_EQ(presentApiError(error, "bg"), Generic);
		CHECK_EQ(presentOnlineProblem("driver socket error: private diagnostics", "bg"), Generic);
		CHECK_EQ(presentOnlineProblem("driver socket error: private diagnostics", "en"), "driver socket error: private diagnostics");
		CHECK_EQ(presentApiError(ApiError{}, "bg"), Generic);
	}
	TEST_CASE("parameters are scalar literal text in source order and substituted once")
	{
		auto error = errorFromJson(Json{{"code", "forbidden"}, {"message", "diagnostic"},
			{"messageKey", "Signed in as {slot0} ({slot1}). Every administrative change is recorded."},
			{"messageParams", {{"slot1", 42}, {"slot0", "User {slot1} %0 <tag>"}}}}, "fallback");
		CHECK_EQ(presentApiError(error, "bg"), "Влезли сте като User {slot1} %0 <tag> (42). Всяка административна промяна се записва.");
		error.messageParams.erase("slot1");
		CHECK_EQ(presentApiError(error, "bg"), Generic);
		error.messageParams["slot1"] = 42;
		error.messageParams["unused"] = false;
		CHECK(presentApiError(error, "bg") != Generic);
		error.messageKey = "{slot0} · {slot1}";
		error.messageParams = Json{{"slot0", true}, {"slot1", nullptr}};
		CHECK_EQ(presentApiError(error, "bg"), "true · null");
	}
	TEST_CASE("decoder rejects malformed localization metadata but leaves original fields intact")
	{
		for (const auto &metadata : {Json{{"messageKey", 12}}, Json{{"messageKey", ""}},
			 Json{{"messageParams", nullptr}}, Json{{"messageParams", Json::array()}},
			 Json{{"messageParams", {{"slot0", Json::object()}}}}})
		{
			Json body = metadata;
			body["code"] = "bad_request";
			body["message"] = "Sign in first (session.authenticate).";
			body["details"] = Json{{"unchanged", Json::array({1, 2})}};
			const auto error = errorFromJson(body, "fallback");
			CHECK(error.invalidLocalization);
			CHECK_EQ(error.code, "bad_request");
			CHECK_EQ(error.message, body["message"].get<std::string>());
			CHECK_EQ(error.details, body["details"]);
			CHECK_EQ(presentApiError(error, "bg"), Generic);
			CHECK_EQ(presentApiError(error, "fr"), body["message"].get<std::string>());
		}
	}
	TEST_CASE("unknown source and malformed direct parameters use reviewed generic")
	{
		ApiError error{"internal", "technical/private error", Json()};
		error.messageKey = "an unknown source key";
		CHECK_EQ(presentApiError(error, "bg"), Generic);
		error.messageKey = "Sign in first (session.authenticate).";
		error.messageParams = Json::array({"bad"});
		CHECK_EQ(presentApiError(error, "bg"), Generic);
		error.messageParams = Json::object();
		error.messageKey = "";
		CHECK_EQ(presentApiError(error, "bg"), Generic);
	}
	TEST_CASE("realtime decoding retains the same metadata as REST errors")
	{
		const Json body = {{"code", "forbidden"}, {"message", "raw"},
			{"messageKey", "{slot0} / {slot1}"}, {"messageParams", {{"slot0", 3}, {"slot1", 7}}}};
		const auto frame = decodeServerMessage(Json{{"type", "response"}, {"id", "c1"}, {"ok", false}, {"error", body}}.dump());
		REQUIRE(frame.kind == ServerMessage::Kind::Response);
		CHECK_FALSE(frame.ok);
		CHECK_EQ(frame.error.message, "raw");
		CHECK_EQ(frame.error.messageParams, body["messageParams"]);
		CHECK_EQ(presentApiError(frame.error, "bg"), "3 / 7");
		CHECK_EQ(errorFromJson(Json::array(), "http_500").code, "http_500");
	}
	TEST_CASE("embedded data exposes source hash metadata and portable chunks")
	{
		CHECK_EQ(std::string(ErrorCatalogData::EnglishSha256).size(), 64);
		CHECK_EQ(std::string(ErrorCatalogData::BulgarianSha256).size(), 64);
		CHECK(ErrorCatalogData::MessageCount > 0);
		CHECK(ErrorCatalogData::ExcludedPluralCount > 0);
		for (auto chunk : ErrorCatalogData::Chunks) CHECK(chunk.size() <= 8192);
	}
}

TEST_CASE("UI exception boundary preserves intentional messages and legacy diagnostics" * doctest::test_suite("ApiErrorText"))
{
	const std::runtime_error raw("Invalid managed AI path /exact/path");
	for (auto locale : {"bg", "bg-BG"})
	{
		CHECK_EQ(Glob2UI::presentException(raw, locale, Generic), Generic);
		const Glob2UI::PresentedError typed("Конкретна грешка {p0}", "raw provider diagnostic");
		const std::exception &base = typed;
		CHECK_EQ(Glob2UI::presentException(base, locale, Generic), "Конкретна грешка {p0}");
		CHECK_EQ(typed.diagnostic, "raw provider diagnostic");
		CHECK_EQ(std::string(typed.what()), "Конкретна грешка {p0}");
		const Glob2UI::PresentedError native("Невалиден отговор", "Invalid AI catalogue entry");
		CHECK_EQ(Glob2UI::presentException(native, locale, Generic), "Невалиден отговор");
	}
	for (auto locale : {"en", "fr", "", "bg-XX"})
	{
		CHECK_EQ(Glob2UI::presentException(raw, locale, Generic), raw.what());
		const Glob2UI::PresentedError native("Невалиден отговор", "Invalid AI catalogue entry");
		CHECK_EQ(Glob2UI::presentException(native, locale, Generic), "Invalid AI catalogue entry");
		const Glob2UI::PresentedError previous("ancien message localisé");
		CHECK_EQ(Glob2UI::presentException(previous, locale, Generic), previous.what());
	}
	const auto api = errorFromJson(Json{{"code", "unauthenticated"}, {"message", "raw auth"},
		{"messageKey", "Sign in first (session.authenticate)."}}, "fallback");
	const Glob2UI::PresentedError marked(presentApiError(api, "bg"), api.message);
	CHECK_EQ(Glob2UI::presentException(marked, "bg", Generic), presentApiError(api, "bg"));
	CHECK_NE(Glob2UI::presentException(marked, "bg", Generic), Generic);
}
