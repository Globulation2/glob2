// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/Glob2Test.h"
#include "../libgag/src/RecordingSession.h"
#include <nlohmann/json.hpp>
#include <fstream>

using namespace GAGCore::Recording;
using namespace GAGCore::Recording::Detail;
namespace
{
std::string output(const char *name)
{
	return (glob2test::artifactDir() / (std::string(name)+"-"+std::to_string(SDL_GetPerformanceCounter())+".mp4")).string();
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
		CHECK(first["width"]==66); CHECK(first["height"]==38);
		CHECK(second["width"]==70); CHECK(second["height"]==42);
		CHECK(second["segment"]==2); CHECK(second["session_start_us"].get<std::int64_t>()>0);
		CHECK(manifest(path+".session.json")["segments"].size()==2);
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
	}
	TEST_CASE("device failure closes its stream and starts a software segment")
	{
		auto path=output("device-loss"); auto files=nativeSessionStorage(); files.reserve(path);
		Status status;
		Session session(path,{},files,[&](const Status &s) { status=s; },std::make_unique<FailingHardware>(VideoConfiguration{66,38,30,23,true}));
		for (int n=0;n<20;++n) { auto time=1000000+std::int64_t(n)*1000000/30; session.frame(pixels(time)); session.step(time+50000); }
		session.stop(1700000); for (int n=0;n<10&&!session.step(1750000);++n) {}
		REQUIRE(status.state==State::Complete); REQUIRE(status.outputs.size()==2);
		CHECK(status.encoder=="libx264"); CHECK(status.fallbackReason=="forced asynchronous device loss");
		CHECK(manifest(status.outputs.front()+".json")["encoder"]=="test-hardware");
		CHECK(manifest(status.outputs.back()+".json")["encoder"]=="libx264");
	}
	TEST_CASE("hardware flush failure during resize continues with a software segment")
	{
		auto path=output("flush-loss"); auto files=nativeSessionStorage(); files.reserve(path); Status status;
		Session session(path,{},files,[&](const Status &s) { status=s; },std::make_unique<FailingHardware>(VideoConfiguration{66,38,30,23,true},true));
		for (int n=0;n<30;++n) { auto time=1000000+std::int64_t(n)*1000000/30; session.frame(pixels(time,n<6?65:69,n<6?37:41)); session.step(time+50000); }
		session.stop(2000000); for (int n=0;n<10&&!session.step(2050000);++n) {}
		REQUIRE(status.state==State::Complete); REQUIRE(status.outputs.size()==2);
		CHECK(status.encoder=="libx264"); CHECK(status.fallbackReason=="forced asynchronous flush failure");
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
		auto path=output("interrupted"); auto files=nativeSessionStorage(); files.reserve(path);
		Options options; options.encoder=EncoderPreference::Software;
		{
			Session session(path,options,files,[](const Status &) {});
			for (int n=0;n<120;++n) { auto time=1000000+std::int64_t(n)*1000000/30; session.frame(pixels(time)); session.step(time+50000); }
			CHECK(std::filesystem::file_size(path+".recording/capture.mp4")>1000);
		}
		SUBCASE("interrupted metadata write retains an alternating checkpoint")
		{ std::ofstream(path+".recording/manifest.json",std::ios::trunc) << "{\"version\":"; }
		recoverRecording(path,nativeSessionStorage());
		CHECK(manifest(path+".json")["recovered"]==true);
		CHECK(manifest(path+".json")["duration_us"].get<std::int64_t>()>=2000000);
	}
	TEST_CASE("publication failure retains media that can be recovered without overwriting")
	{
		auto path=output("recover"); auto files=nativeSessionStorage(); files.reserve(path);
		files.publish=[](const auto &,const auto &) { throw std::runtime_error("forced publication failure"); };
		Options options; options.encoder=EncoderPreference::Software;
		Status status;
		{
			Session session(path,options,files,[&](const Status &s) { status=s; });
			session.frame(pixels(1000000)); session.step(1000000); session.stop(1033333);
			try { session.step(1100000); FAIL("Publication failure must be reported"); }
			catch (const std::exception &e) { session.fail(e.what()); }
			CHECK(status.state==State::Failed);
		}
		CHECK_FALSE(manifest(path+".recording/manifest.json")["complete"].get<bool>());
		recoverRecording(path,nativeSessionStorage());
		CHECK(manifest(path+".json")["recovered"]==true);
		CHECK_FALSE(std::filesystem::exists(path+".recording"));
	}
}
