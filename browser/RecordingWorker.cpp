// SPDX-License-Identifier: GPL-3.0-or-later
#include "../libgag/src/RecordingSession.h"
#include <emscripten.h>
#include <nlohmann/json.hpp>
#include <deque>
#include <map>
#include <stdexcept>
#include <cstring>

using namespace GAGCore::Recording;
using namespace GAGCore::Recording::Detail;
namespace
{
using Json = nlohmann::json;
std::unique_ptr<Session> session;
EM_JS(int, storageOpen, (const char *path, int write), { return recordingStorage.open(UTF8ToString(path),Boolean(write)); });
EM_JS(int, storageRead, (int id, unsigned char *bytes, int size), { return recordingStorage.read(id,HEAPU8.subarray(bytes,bytes+size)); });
EM_JS(int, storageWrite, (int id, const unsigned char *bytes, int size), { return recordingStorage.write(id,HEAPU8.subarray(bytes,bytes+size)); });
EM_JS(double, storageSeek, (int id, double offset, int whence), { return recordingStorage.seek(id,offset,whence); });
EM_JS(int, storageFlush, (int id), { return recordingStorage.flush(id); });
EM_JS(void, storageClose, (int id), { recordingStorage.close(id); });
EM_JS(int, storageReserve, (const char *path), { return recordingStorage.reserve(UTF8ToString(path)); });
EM_JS(int, storagePublish, (const char *work, const char *path), { return recordingStorage.publish(UTF8ToString(work),UTF8ToString(path)); });
EM_JS(void, storageCleanup, (const char *path), { recordingStorage.cleanup(UTF8ToString(path)); });
EM_JS(void, report, (const char *value), { postMessage({type:'status',status:JSON.parse(UTF8ToString(value))}); });
EM_JS(int, webOpen, (int width, int height, int fps), { return recordingVideo.open(width,height,fps); });
EM_JS(void, webClose, (int id), { recordingVideo.close(id); });
EM_JS(int, webReady, (int id), { return recordingVideo.ready(id); });
EM_JS(int, webSubmit, (int id, const unsigned char *rgba, int width, int height, double pts), { return recordingVideo.submit(id,HEAPU8.subarray(rgba,rgba+width*height*4),width,height,pts); });
EM_JS(int, webFinish, (int id), { return recordingVideo.finish(id); });
class OPFSFile final : public RecordingFile
{
	int id;
  public:
	OPFSFile(const std::string &path, bool write) : id(storageOpen(path.c_str(),write)) { if (id < 0) throw std::runtime_error("Recording storage is unavailable"); }
	~OPFSFile() override { storageClose(id); }
	int read(unsigned char *bytes,int size) override { return storageRead(id,bytes,size); }
	int write(const unsigned char *bytes,int size) override { return storageWrite(id,bytes,size); }
	std::int64_t seek(std::int64_t offset,int whence) override { return std::int64_t(storageSeek(id,double(offset),whence)); }
	void flush() override { if (storageFlush(id)<0) throw std::runtime_error("Recording storage is full or unavailable"); }
};
class WebVideo;
std::map<int,WebVideo *> encoders;
class WebVideo final : public VideoEncoder
{
	int id;
	VideoDescription info;
	std::deque<VideoPacket> packets;
  public:
	explicit WebVideo(const VideoConfiguration &c) : id(webOpen(c.width,c.height,c.fps))
	{
		if (id < 0) throw std::runtime_error("WebCodecs H.264 configuration unavailable");
		info.width = c.width; info.height = c.height; info.encoder = "webcodecs";
		encoders[id] = this;
	}
	~WebVideo() override { encoders.erase(id); webClose(id); }
	const VideoDescription &description() const override { return info; }
	bool ready() const override { return webReady(id); }
	void submit(const unsigned char *rgba,int w,int h,std::int64_t pts) override
	{
		if (webSubmit(id,rgba,w,h,double(pts))<0) throw std::runtime_error("WebCodecs H.264 encoder failed");
	}
	bool receive(VideoPacket &packet) override
	{
		if (packets.empty()) return false;
		packet = std::move(packets.front()); packets.pop_front(); return true;
	}
	bool finish() override
	{
		int result = webFinish(id);
		if (result < 0) throw std::runtime_error("WebCodecs H.264 flush failed");
		return result;
	}
	void packet(VideoPacket packet) { if (packets.size() >= 3) throw std::runtime_error("WebCodecs output queue exceeded its budget"); packets.push_back(std::move(packet)); }
};
SessionStorage storage()
{
	SessionStorage s;
	s.open = [](const std::string &path,bool write) { return std::make_unique<OPFSFile>(path,write); };
	s.reserve = [](const std::string &path) { if (storageReserve(path.c_str())<0) throw std::runtime_error("Recording output already exists or is reserved"); };
	s.publish = [](const std::string &work,const std::string &path) { if (storagePublish(work.c_str(),path.c_str())<0) throw std::runtime_error("Cannot publish recording"); };
	s.cleanup = [](const std::string &path) { storageCleanup(path.c_str()); };
	s.video = [](const VideoConfiguration &c) -> std::unique_ptr<VideoEncoder>
	{
		std::string reason;
		if (!c.software) { try { return std::make_unique<WebVideo>(c); } catch (const std::exception &e) { reason = e.what(); } }
		auto copy = c; copy.software = true; return createVideoEncoder(copy,reason);
	};
	return s;
}
void guarded(const std::function<void()> &action)
{
	try { action(); }
	catch (const std::exception &e) { if (session) session->fail(e.what()); else { auto value = Json{{"state",int(State::Failed)},{"error",e.what()}}.dump(); report(value.c_str()); } }
}
}
extern "C"
{
EMSCRIPTEN_KEEPALIVE void glob2_record_recover(const char *path)
{
	guarded([&] { recoverRecording(path,storage()); auto value = Json{{"state",int(State::Complete)},{"outputs",Json::array({path})},{"error",""}}.dump(); report(value.c_str()); });
}
EMSCRIPTEN_KEEPALIVE void glob2_record_start(const char *path,int fps,int crf,int software,int chapters)
{
	guarded([&]
	{
		session.reset(); auto files = storage(); files.reserve(path);
		Options options; options.chapterTicks = chapters > 0 ? chapters : 10000; options.fps = fps; options.crf = crf; options.encoder = software ? EncoderPreference::Software : EncoderPreference::Auto;
		session = std::make_unique<Session>(path,options,std::move(files),[](const Status &s)
		{
			auto value = Json{{"state",int(s.state)},{"path",s.path},{"error",s.error},{"encoder",s.encoder},
				{"fallbackReason",s.fallbackReason},{"segment",s.segment},{"width",s.width},{"height",s.height},{"sourceWidth",s.sourceWidth},{"sourceHeight",s.sourceHeight},
				{"outputs",s.outputs},{"droppedFrames",s.droppedFrames},{"droppedAudioSamples",s.droppedAudioSamples}}.dump(); report(value.c_str());
		});
	});
}
EMSCRIPTEN_KEEPALIVE void glob2_record_frame(const unsigned char *pixels,int width,int height,double time,const char *context)
{
	guarded([&]
	{
		if (!session) return;
		CaptureFrame f; f.time = std::int64_t(time); f.width = width; f.height = height;
		auto c = Json::parse(context); f.pixelLayout = c.value("pixel_layout",0u); f.bottomUp = c.value("bottom_up",false);
		f.rgba.assign(pixels,pixels+std::size_t(width)*height*(f.pixelLayout<4 ? 4 : 3));
		 f.context.screen = c.value("screen","startup"); f.context.dialog = c.value("dialog","");
		f.context.mode = c.value("mode",""); f.context.map = c.value("map",""); f.context.match = c.value("match",std::uint64_t(0));
		f.context.tick = c.value("tick",0u); f.context.team = c.value("team",-1); f.context.speed = c.value("speed",0); f.context.paused = c.value("paused",false);
		session->frame(std::move(f));
	});
}
EMSCRIPTEN_KEEPALIVE void glob2_record_audio(const std::int16_t *pcm,int count,double time) { guarded([&] { if (session) session->audio(pcm,count,std::int64_t(time)); }); }
EMSCRIPTEN_KEEPALIVE void glob2_record_event(double time,const char *kind,const char *value) { guarded([&] { if (session) session->event(std::int64_t(time),kind,value); }); }
EMSCRIPTEN_KEEPALIVE void glob2_record_drops(double frames,double audio) { if (session) session->drops(std::uint64_t(frames),std::uint64_t(audio)); }
EMSCRIPTEN_KEEPALIVE int glob2_record_queued() { return session ? int(session->queuedFrames()) : 0; }
EMSCRIPTEN_KEEPALIVE int glob2_record_step(double time)
{
	int done = 0; guarded([&] { if (session) done = session->step(std::int64_t(time)); }); return done;
}
EMSCRIPTEN_KEEPALIVE void glob2_record_stop(double time) { if (session) session->stop(std::int64_t(time)); }
EMSCRIPTEN_KEEPALIVE void glob2_record_fail(const char *error) { if (session) session->fail(error); }
EMSCRIPTEN_KEEPALIVE void glob2_web_packet(int id,const unsigned char *bytes,int size,double pts,double duration,int key)
{
	guarded([&] { auto found = encoders.find(id); if (found == encoders.end()) return;
		VideoPacket p; p.bytes.assign(bytes,bytes+size); p.ptsUs = p.dtsUs = std::int64_t(pts); p.durationUs = std::int64_t(duration); p.key = key;
		found->second->packet(std::move(p)); });
}
}
