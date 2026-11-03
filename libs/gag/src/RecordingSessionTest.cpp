// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/Glob2Test.h"
#include "support/RecordingValidationProcess.h"
#include "RecordingSession.h"
#include <nlohmann/json.hpp>
#include <fstream>
extern "C"
{
#include <libavcodec/avcodec.h>
}

using namespace GAGCore::Recording;
using namespace GAGCore::Recording::Detail;
namespace
{
std::string output(const char *name)
{
	return (glob2test::artifactDir() / (std::string(name)+"-"+std::to_string(SDL_GetPerformanceCounter())+Extension)).string();
}
CaptureFrame pixels(std::int64_t time,int width=65,int height=37)
{
	CaptureFrame frame; frame.time=time; frame.width=width; frame.height=height;
	frame.rgba.assign(std::size_t(width)*height*4,127); frame.context.screen="main_menu"; return frame;
}
nlohmann::json manifest(const std::string &path)
{
	std::ifstream input(path); return nlohmann::json::parse(input);
}
std::vector<nlohmann::json> journal(const std::string &path)
{
	std::ifstream input(path); std::string line;
	std::vector<nlohmann::json> entries;
	while (std::getline(input,line)) entries.push_back(nlohmann::json::parse(line));
	return entries;
}
void checkJournalOrder(const std::vector<nlohmann::json> &entries)
{
	std::int64_t previous = -1;
	for (const auto &entry : entries)
	{
		const auto time = entry.at("time_us").get<std::int64_t>();
		CHECK(time>=previous); previous=time;
	}
}

class DeferredFlush final : public VideoEncoder
{
	std::unique_ptr<VideoEncoder> inner;
	bool requested=false;
  public:
	explicit DeferredFlush(VideoConfiguration configuration) : inner(createVideoEncoder(configuration)) {}
	const VideoDescription &description() const override { return inner->description(); }
	void submit(const unsigned char *p,int w,int h,std::int64_t time) override { inner->submit(p,w,h,time); }
	bool receive(VideoPacket &packet) override { return inner->receive(packet); }
	bool finish() override { if (!requested) { requested=true; return false; } return inner->finish(); }
};
class FrameProbe final : public VideoEncoder
{
    std::unique_ptr<VideoEncoder> inner;
    std::vector<unsigned char> &captured;
  public:
    explicit FrameProbe(std::vector<unsigned char> &captured) : inner(createVideoEncoder({2,2,30,23,true})),captured(captured) {}
    const VideoDescription &description() const override { return inner->description(); }
    void submit(const unsigned char *p,int w,int h,std::int64_t time) override
    { captured.assign(p,p+std::size_t(w)*h*4); inner->submit(p,w,h,time); }
    bool receive(VideoPacket &packet) override { return inner->receive(packet); }
    bool finish() override { return inner->finish(); }
};
class FailingHardware final : public VideoEncoder
{
	std::unique_ptr<VideoEncoder> inner;
	VideoDescription info;
	int frames=0;
	bool flushFailure=false;
  public:
	explicit FailingHardware(VideoConfiguration configuration,bool flushFailure=false) : inner(createVideoEncoder(configuration)),info(inner->description()),flushFailure(flushFailure) { info.encoder="test-hardware"; }
	const VideoDescription &description() const override { return info; }
	void submit(const unsigned char *p,int w,int h,std::int64_t time) override
	{ if (++frames==4 && !flushFailure) throw std::runtime_error("forced asynchronous device loss"); inner->submit(p,w,h,time); }
	bool receive(VideoPacket &packet) override { return inner->receive(packet); }
	bool finish() override { if (flushFailure) throw std::runtime_error("forced asynchronous flush failure"); return inner->finish(); }
};

}
TEST_SUITE("RecordingSession")
{
	TEST_CASE("selected profile includes only its recording encoders")
	{
		CHECK(avcodec_find_encoder_by_name(SoftwareEncoder) != nullptr);
		CHECK(avcodec_find_encoder_by_name(WebM ? "libopus" : "aac") != nullptr);
		if (WebM)
		{
			CHECK(avcodec_find_encoder(AV_CODEC_ID_H264) == nullptr);
			CHECK(avcodec_find_encoder(AV_CODEC_ID_AAC) == nullptr);
			CHECK(avcodec_find_encoder_by_name("libx264") == nullptr);
		}
	}
	TEST_CASE("AV1 writer rejects audio and finalization until its sequence header arrives")
	{
		if (std::string(VideoCodec) != "av1") return;
		auto encoder = createVideoEncoder({64,64,30,23,true});
		auto description = encoder->description(); description.extraData.clear();
		const auto path = output("audio-before-video");
		MediaWriter writer(path,description);
		std::vector<std::int16_t> samples(960*2,0);
		CHECK_THROWS_AS(writer.audio(samples.data(),960,0),std::runtime_error);
		CHECK_THROWS_AS(writer.silenceThrough(960),std::runtime_error);
		CHECK_THROWS_AS(writer.finish(20000),std::runtime_error);
		CHECK(std::filesystem::file_size(path)==0);

		// Rejected calls must not consume samples or poison the writer.
		std::vector<unsigned char> rgba(64*64*4,128);
		encoder->submit(rgba.data(),64,64,0);
		VideoPacket packet;
		REQUIRE(encoder->receive(packet)); REQUIRE_FALSE(packet.extraData.empty());
		writer.video(packet); writer.audio(samples.data(),960,0);
		REQUIRE(encoder->finish());
		while (encoder->receive(packet)) writer.video(packet);
		writer.finish(20000);
		CHECK(std::filesystem::file_size(path)>0);
	}
	TEST_CASE("session index creation preserves an existing recording")
	{
		auto path = output("exclusive") + ".session.json";
		const std::string original = "{\"segments\":[]}";
		{
			auto file = openNativeRecordingFile(path, true);
			REQUIRE(file->write(reinterpret_cast<const unsigned char *>(original.data()), original.size()) == int(original.size()));
			file->flush();
		}
		CHECK_THROWS_AS(openNativeRecordingFile(path, true), std::runtime_error);
		std::ifstream saved(path, std::ios::binary);
		CHECK(std::string(std::istreambuf_iterator<char>(saved), {}) == original);
	}

	TEST_CASE("control events wait for delayed video chapters without changing their timestamps")
	{
		const auto path=output("event-order"); auto files=nativeSessionStorage(); files.reserve(path);
		Options options; options.encoder=EncoderPreference::Software;
		Status status; Session session(path,options,files,[&](const Status &value) { status=value; });
		constexpr std::int64_t start=1000000;
		for (int n=0;n<30;++n)
		{
			const auto time=start+std::int64_t(n)*1000000/30;
			session.frame(pixels(time)); session.step(time+50000);
		}
		session.event(start+1036673,"paused","true");
		auto first=pixels(start+1000000); first.context.screen="settings";
		session.frame(std::move(first)); session.step(start+1050000);
		CHECK(journal(path+".recording/events.jsonl").size()==2);
		auto second=pixels(start+1033333); second.context.screen="end_game";
		session.frame(std::move(second)); session.step(start+1083333);
		// A genuinely late input cannot be appended honestly after newer entries.
		CHECK_THROWS_AS(session.event(start+1000000,"late","rejected"),std::runtime_error);
		session.stop(start+1100000);
		for (int n=0;n<10 && !session.step(start+1150000);++n) {}
		REQUIRE(status.state==State::Complete);
		const auto entries=journal(path+".events.jsonl"); checkJournalOrder(entries);
		REQUIRE(entries.size()==4);
		CHECK(entries[1]["time_us"]==1000000); CHECK(entries[1]["kind"]=="chapter");
		CHECK(entries[2]["time_us"]==1033333); CHECK(entries[2]["kind"]=="chapter");
		CHECK(entries[3]["time_us"]==1036673); CHECK(entries[3]["kind"]=="paused");
	}
	TEST_CASE("resize carries future controls to the new segment and journals one transition")
	{
		const auto path=output("resize-events"); auto files=nativeSessionStorage(); files.reserve(path);
		Options options; options.encoder=EncoderPreference::Software;
		Status status;
		Session session(path,options,files,[&](const Status &value) { status=value; },
			std::make_unique<DeferredFlush>(VideoConfiguration{66,38,30,23,true}));
		constexpr std::int64_t start=1000000;
		for (int n=0;n<16;++n)
		{
			const auto time=start+std::int64_t(n)*1000000/30;
			session.frame(pixels(time,n<10?65:69,n<10?37:41)); session.step(time+50000);
		}
		session.event(start+560000,"speed","2");
		session.frame(pixels(start+533333,69,41));
		CHECK_FALSE(session.step(start+583333)); CHECK(status.outputs.empty());
		CHECK_FALSE(session.step(start+583334)); REQUIRE(status.segment==2);
		session.stop(start+566666); REQUIRE(session.step(start+650000));
		REQUIRE(status.state==State::Complete); REQUIRE(status.outputs.size()==2);
		const auto first=journal(path+".events.jsonl"); checkJournalOrder(first);
		const auto duration=manifest(path+".json")["duration_us"].get<std::int64_t>();
		for (const auto &entry : first) CHECK(entry["time_us"].get<std::int64_t>()<duration);
		const auto next=journal(status.outputs.back()+".events.jsonl"); checkJournalOrder(next);
		int transitions=0,controls=0;
		const auto offset=manifest(status.outputs.back()+".json")["session_start_us"].get<std::int64_t>();
		for (const auto &entry : next)
		{
			if (entry["kind"]=="resize") { ++transitions; CHECK(entry["time_us"]==0); }
			if (entry["kind"]=="speed")
			{
				++controls; CHECK(entry["time_us"].get<std::int64_t>()+offset==560000);
			}
		}
		CHECK(transitions==1); CHECK(controls==1);
	}
	TEST_CASE("stop rejects events beyond the final video interval and retains diagnostics")
	{
		const auto path=output("event-after-stop"); auto files=nativeSessionStorage(); files.reserve(path);
		Options options; options.encoder=EncoderPreference::Software;
		Status status; Session session(path,options,files,[&](const Status &value) { status=value; });
		session.frame(pixels(1000000)); session.step(1050000);
		session.event(1100000,"future","retained for diagnosis");
		session.stop(1033333);
		try { session.step(1100000); FAIL("An event after the final video interval must be rejected"); }
		catch (const std::runtime_error &error) { session.fail(error.what()); }
		REQUIRE(status.state==State::Failed);
		CHECK(status.error.find("past the final video interval")!=std::string::npos);
		CHECK_FALSE(std::filesystem::exists(path));
		CHECK_FALSE(std::filesystem::exists(path+".json"));
		CHECK_FALSE(manifest(path+".recording/manifest.json")["complete"].get<bool>());
		CHECK(std::filesystem::exists(path+".recording/capture"+Extension));
		const auto entries=journal(path+".recording/events.jsonl"); checkJournalOrder(entries);
		REQUIRE(entries.size()==2); CHECK(entries.back()["time_us"]==100000);
		CHECK(entries.back()["kind"]=="future");
	}
	TEST_CASE("journal backlog is bounded and failure retains queued diagnostic events")
	{
		const auto path=output("event-budget"); auto files=nativeSessionStorage(); files.reserve(path);
		Options options; options.encoder=EncoderPreference::Software;
		Status status; Session session(path,options,files,[&](const Status &value) { status=value; });
		session.frame(pixels(1000000)); session.step(1050000);
		for (int n=0;n<1024;++n) session.event(2000000+n,"pending","value");
		CHECK_THROWS_AS(session.event(2001024,"overflow","rejected"),std::runtime_error);
		session.fail("forced journal budget failure"); CHECK(status.state==State::Failed);
		const auto entries=journal(path+".recording/events.jsonl"); checkJournalOrder(entries);
		REQUIRE(entries.size()==1025); CHECK(entries.back()["time_us"]==1001023);
	}
	TEST_CASE("resize coalescing preserves full resolution and numbers completed segments")
	{
		auto path=output("resize"); auto files=nativeSessionStorage(); files.reserve(path);
		Options options; options.encoder=EncoderPreference::Software;
		Status status; Session session(path,options,files,[&](const Status &s) { status=s; });
		constexpr std::int64_t start=1000000;
		for (int n=0;n<30;++n)
		{
			auto time=start+std::int64_t(n)*1000000/30;
			session.frame(pixels(time,n<6?65:n<9?67:69,n<6?37:n<9?39:41)); session.step(time);
		}
		session.stop(start+1000000);
		for (int n=0;n<10 && !session.step(start+1000000);++n) {}
		REQUIRE(status.state==State::Complete); REQUIRE(status.outputs.size()==2);
		auto first=manifest(path+".json"),second=manifest(status.outputs.back()+".json");
		CHECK(first["video_codec"]==VideoCodec); CHECK(first["audio_codec"]==AudioCodec);
		CHECK(std::filesystem::path(status.outputs.back()).extension()==Extension);
		CHECK(first["width"]==66); CHECK(first["height"]==38);
		CHECK(second["width"]==70); CHECK(second["height"]==42);
		CHECK(second["segment"]==2); CHECK(second["session_start_us"].get<std::int64_t>()>0);
		CHECK(manifest(path+".session.json")["segments"].size()==2);
		if (const char *ffmpeg = SDL_getenv("GLOB2_TEST_FFMPEG"))
		{
			const auto ffprobe = std::filesystem::path(ffmpeg).parent_path() / "ffprobe";
			Process probe;
			probe.launch({ffprobe.string(), "-v", "error", "-show_streams", "-show_chapters",
				"-of", "json", "-o", path+".probe.json", path}, path+".probe.log", false);
			REQUIRE(probe.finish()==0);
			const auto media = manifest(path+".probe.json");
			REQUIRE(media["streams"].size()>=2);
			CHECK(media["streams"][0]["codec_name"]==VideoCodec);
			CHECK(media["streams"][1]["codec_name"]==AudioCodec);
			CHECK(media["streams"][1]["sample_rate"]=="48000");
			CHECK(media["chapters"].size()==first["chapters"].size());
			Process decode;
			decode.launch({ffmpeg, "-v", "error", "-xerror", "-nostdin", "-i", path,
				"-f", "null", "-"}, path+".decode.log", false);
			CHECK(decode.finish()==0);
		}
	}
	TEST_CASE("worker normalizes capture color layout and bottom-up orientation")
	{
		auto path=output("orientation"); auto files=nativeSessionStorage(); files.reserve(path);
		std::vector<unsigned char> captured; Status status;
		Session session(path,{},files,[&](const Status &s) { status=s; },std::make_unique<FrameProbe>(captured));
		auto frame=pixels(1000000,2,2); frame.pixelLayout=1; frame.bottomUp=true;
		frame.rgba={255,0,0,255,255,0,0,255,0,0,255,255,0,0,255,255};
		session.frame(std::move(frame)); session.step(1000000);
		REQUIRE(captured.size()==16); CHECK(captured[0]==255); CHECK(captured[2]==0);
		CHECK(captured[8]==0); CHECK(captured[10]==255);
		session.stop(1033333); CHECK(session.step(1100000));
		CHECK(status.state==State::Complete);
	}
	TEST_CASE("asynchronous encoder flush is polled before publishing the output")
	{
		auto path=output("deferred"); auto files=nativeSessionStorage(); files.reserve(path);
		Status status;
		Session session(path,{},files,[&](const Status &s) { status=s; },std::make_unique<DeferredFlush>(VideoConfiguration{66,38,30,23,true}));
		session.frame(pixels(1000000)); session.step(1000000); session.stop(1033333);
		CHECK_FALSE(session.step(1100000)); CHECK(status.state==State::Finalizing);
		CHECK_FALSE(std::filesystem::exists(path));
		CHECK(session.step(1100001)); CHECK(status.state==State::Complete);
	}
	TEST_CASE("long scheduling gaps preserve time without an encoding catch-up burst")
	{
		auto path=output("gap"); auto files=nativeSessionStorage(); files.reserve(path);
		Options options; options.encoder=EncoderPreference::Software;
		Status status; Session session(path,options,files,[&](const Status &s) { status=s; });
		session.frame(pixels(1000000)); session.step(1000000);
		session.frame(pixels(3601000000)); session.step(3601050000); session.stop(3601033333);
		REQUIRE(session.step(3601100000)); CHECK(status.state==State::Complete);
		CHECK(std::filesystem::file_size(path)<100000);
		CHECK(manifest(path+".json")["duration_us"].get<std::int64_t>()>=3600000000);
		if (const char *validator = SDL_getenv("GLOB2_TEST_FFMPEG"))
		{
			Process probe;
			probe.launch({validator,"-v","error","-nostdin","-i",path,"-map","0:v:0",
				"-c","copy","-f","null","-","-progress",path+".progress"},path+".probe.log",false);
			REQUIRE(probe.finish()==0);
			std::ifstream input(path+".progress"); std::string line; std::int64_t end=0;
			while (std::getline(input,line)) if (line.starts_with("out_time_us=")) end=std::stoll(line.substr(12));
			CHECK(end>=3600000000);
		}
	}
	TEST_CASE("device failure closes its stream and starts a software segment")
	{
		auto path=output("device-loss"); auto files=nativeSessionStorage(); files.reserve(path);
		Status status;
		Session session(path,{},files,[&](const Status &s) { status=s; },std::make_unique<FailingHardware>(VideoConfiguration{66,38,30,23,true}));
		for (int n=0;n<20;++n) { auto time=1000000+std::int64_t(n)*1000000/30; session.frame(pixels(time)); session.step(time+50000); }
		session.stop(1700000); for (int n=0;n<10&&!session.step(1750000);++n) {}
		REQUIRE(status.state==State::Complete); REQUIRE(status.outputs.size()==2);
		CHECK(status.encoder==SoftwareEncoder); CHECK(status.fallbackReason=="forced asynchronous device loss");
		CHECK(manifest(status.outputs.front()+".json")["encoder"]=="test-hardware");
		CHECK(manifest(status.outputs.back()+".json")["encoder"]==SoftwareEncoder);
	}
	TEST_CASE("hardware flush failure during resize continues with a software segment")
	{
		auto path=output("flush-loss"); auto files=nativeSessionStorage(); files.reserve(path); Status status;
		Session session(path,{},files,[&](const Status &s) { status=s; },std::make_unique<FailingHardware>(VideoConfiguration{66,38,30,23,true},true));
		for (int n=0;n<30;++n) { auto time=1000000+std::int64_t(n)*1000000/30; session.frame(pixels(time,n<6?65:69,n<6?37:41)); session.step(time+50000); }
		session.stop(2000000); for (int n=0;n<10&&!session.step(2050000);++n) {}
		REQUIRE(status.state==State::Complete); REQUIRE(status.outputs.size()==2);
		CHECK(status.encoder==SoftwareEncoder); CHECK(status.fallbackReason=="forced asynchronous flush failure");
	}
	TEST_CASE("capture pressure rejects input within the three-frame budget")
	{
		auto path=output("pressure"); auto files=nativeSessionStorage(); files.reserve(path);
		Status status; Options options; options.encoder=EncoderPreference::Software;
		Session session(path,options,files,[&](const Status &s) { status=s; });
		for (int n=0;n<20;++n) session.frame(pixels(1000000+n*33333));
		CHECK(session.queuedFrames()==3); CHECK(status.droppedFrames==17);
		session.step(1100000); session.stop(1100000);
		bool complete=false; for (int n=0;n<10 && !(complete=session.step(1150000));++n) {}
		CHECK(complete);
		CHECK(status.state==State::Complete);
	}
	TEST_CASE("interrupted fragmented media recovers without a finalized trailer")
	{
		for (bool interruptedMetadata : {false,true})
		{
			auto path=output("interrupted"); auto files=nativeSessionStorage(); files.reserve(path);
			Options options; options.encoder=EncoderPreference::Software;
			{
				Session session(path,options,files,[](const Status &) {});
				for (int n=0;n<120;++n) { auto time=1000000+std::int64_t(n)*1000000/30; session.frame(pixels(time)); session.step(time+50000); }
				CHECK(std::filesystem::file_size(path+".recording/capture"+Extension)>1000);
			}
			if (interruptedMetadata)
			{ std::ofstream(path+".recording/manifest.json",std::ios::trunc) << "{\"version\":"; }
			recoverRecording(path,nativeSessionStorage());
			CHECK(manifest(path+".json")["recovered"]==true);
			CHECK(manifest(path+".json")["duration_us"].get<std::int64_t>()>=2000000);
		}
	}
	TEST_CASE("legacy 44.1 kHz recording recovery preserves its audio rate [recording][artifacts]")
	{
		const char *ffmpeg = SDL_getenv("GLOB2_TEST_FFMPEG");
		if (!ffmpeg) return;
		const auto ffprobe = std::filesystem::path(ffmpeg).parent_path() / "ffprobe";
		if (!std::filesystem::exists(ffprobe)) return;
		const auto path = std::filesystem::path(output("legacy-audio")).replace_extension(".mp4").string();
		auto files = nativeSessionStorage(); files.reserve(path);
		Process generate;
		generate.launch({ffmpeg, "-v", "error", "-nostdin", "-f", "lavfi", "-i",
			"color=c=blue:s=64x64:r=30", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100",
			"-t", "0.5", "-c:v", "libx264", "-c:a", "aac", "-ar", "44100", "-ac", "2",
			"-movflags", "+frag_keyframe+empty_moov", path + ".recording/capture.mp4"},
			path + ".generate.log", false);
		REQUIRE(generate.finish() == 0);
		std::ofstream(path + ".recording/manifest.json") << nlohmann::json{
			{"version", 1}, {"video", std::filesystem::path(path).filename().string()},
			{"chapters", nlohmann::json::array()}};
		std::ofstream(path + ".recording/events.jsonl");
		recoverRecording(path, files);
		CHECK(manifest(path + ".json")["recovered"] == true);
		Process probe;
		probe.launch({ffprobe.string(), "-v", "error", "-select_streams", "a:0",
			"-show_entries", "stream=sample_rate", "-of", "default=nw=1:nk=1", path},
			path + ".probe.log", false);
		REQUIRE(probe.finish() == 0);
		std::ifstream rate(path + ".probe.log"); std::string value; rate >> value;
		CHECK(value == "44100");
	}
	TEST_CASE("publication failure retains media that can be recovered without overwriting")
	{
		auto path=output("recover"); auto files=nativeSessionStorage(); files.reserve(path);
		files.publish=[](const auto &,const auto &) { throw std::runtime_error("forced publication failure"); };
		Options options; options.encoder=EncoderPreference::Software; options.fps=12;
		Status status;
		{
			Session session(path,options,files,[&](const Status &s) { status=s; });
			session.frame(pixels(1000000)); session.step(1000000); session.stop(1083333);
			try { session.step(1100000); FAIL("Publication failure must be reported"); }
			catch (const std::exception &e) { session.fail(e.what()); }
			CHECK(status.state==State::Failed);
		}
		CHECK_FALSE(manifest(path+".recording/manifest.json")["complete"].get<bool>());
		recoverRecording(path,nativeSessionStorage());
		CHECK(manifest(path+".json")["recovered"]==true);
		CHECK(std::abs(manifest(path+".json")["duration_us"].get<std::int64_t>()-1000000/12)<=1000);
		CHECK_FALSE(std::filesystem::exists(path+".recording"));
	}
}
