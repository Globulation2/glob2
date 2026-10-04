// SPDX-License-Identifier: GPL-3.0-or-later
#include "RecordingSession.h"
#include <algorithm>
#include <array>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace GAGCore::Recording::Detail
{
namespace
{
std::string utf8(const std::filesystem::path &path)
{
	auto s = path.u8string(); return {reinterpret_cast<const char *>(s.data()), s.size()};
}
void text(const SessionStorage &storage, const std::string &path, const std::string &value)
{
	auto file = storage.open(path, true);
	if (file->write(reinterpret_cast<const unsigned char *>(value.data()), int(value.size())) != int(value.size()))
		throw std::runtime_error("Cannot write recording metadata");
	file->flush();
}
struct Chapter
{
	std::int64_t start, end;
	Context context;
	std::uint32_t lastTick;
};
}
SessionStorage nativeSessionStorage()
{
	SessionStorage storage;
	storage.reserve = [](const std::string &path)
	{
		auto p = std::filesystem::u8path(path);
		std::filesystem::create_directories(p.parent_path());
		for (const auto &suffix : {"", ".json", ".events.jsonl", ".session.json"})
			if (std::filesystem::exists(std::filesystem::u8path(path + suffix)))
				throw std::runtime_error("Recording output already exists; choose a new name");
		if (!std::filesystem::create_directory(std::filesystem::u8path(path + ".recording")))
			throw std::runtime_error("Recording output is reserved; choose a new name");
	};
	storage.publish = [](const std::string &work, const std::string &path)
	{
		publishCompletedRecording(std::filesystem::u8path(work), std::filesystem::u8path(path));
	};
	storage.cleanup = [](const std::string &path)
	{
		std::error_code error; std::filesystem::remove_all(std::filesystem::u8path(path), error);
	};
	return storage;
}
void recoverRecording(const std::string &path, const SessionStorage &storage)
{
	const auto work = path+".recording/";
	// Alternating checkpoints preserve the previous valid metadata when a write
	// is interrupted or storage fills. Read one bounded candidate at a time.
	nlohmann::json manifest;
	std::uint64_t generation = 0;
	for (const char *name : {"manifest.json", "manifest.1.json", "manifest.2.json"})
	{
		try
		{
			auto input = storage.open(work+name,false);
			std::string bytes; std::array<unsigned char,4096> buffer;
			for (;;)
			{
				int n = input->read(buffer.data(),int(buffer.size())); if (n == -541478725) break;
				if (n <= 0 || bytes.size()+std::size_t(n) > 16*1024*1024) throw std::runtime_error("Cannot read recording recovery metadata");
				bytes.append(reinterpret_cast<const char *>(buffer.data()),n);
			}
			auto candidate = nlohmann::json::parse(bytes);
			auto sequence = candidate.value("checkpoint_generation",std::uint64_t(0));
			if (candidate.at("version") == 1 && candidate.at("video") == utf8(std::filesystem::u8path(path).filename()) && (manifest.is_null() || sequence > generation))
			{ manifest = std::move(candidate); generation = sequence; }
		}
		catch (const std::exception &) {} // A missing or incomplete checkpoint is expected.
	}
	if (manifest.is_null()) throw std::runtime_error("No valid recording recovery metadata remains");
	if (manifest.at("version") != 1 || manifest.at("video") != utf8(std::filesystem::u8path(path).filename()))
		throw std::runtime_error("Recording recovery metadata does not match its output");
	auto duration = MediaWriter::duration(work+"capture.mp4",storage.open);
	std::vector<MediaChapter> chapters;
	for (const auto &c : manifest.at("chapters"))
	{
		auto start = c.at("start_us").get<std::int64_t>();
		auto end = std::min(duration,c.at("end_us").get<std::int64_t>());
		if (start < 0 || (!chapters.empty() && start < chapters.back().endUs)) throw std::runtime_error("Invalid recording chapter intervals");
		if (end > start) chapters.push_back({start,end,c.at("title").get<std::string>()});
	}
	if (!chapters.empty()) chapters.back().endUs = duration;
	MediaWriter::finalize(work+"capture.mp4",work+"final.mp4",chapters,storage.open);
	manifest["duration_us"] = duration; manifest["complete"] = true; manifest["error"] = ""; manifest["recovered"] = true;
	auto &list = manifest["chapters"];
	while (!list.empty() && list.back()["start_us"].get<std::int64_t>() >= duration) list.erase(list.size()-1);
	for (auto &c : list) c["end_us"] = std::min(duration,c["end_us"].get<std::int64_t>());
	if (!list.empty()) list.back()["end_us"] = duration;
	text(storage,work+"manifest.json",manifest.dump()+"\n");
	try { storage.publish(work,path); }
	catch (...) { manifest["complete"] = false; text(storage,work+"manifest.json",manifest.dump()+"\n"); throw; }
	storage.cleanup(work);
}
struct Session::Impl
{
	std::string base, path, work;
	Options options;
	SessionStorage storage;
	std::function<void(const Status &)> report;
	Status status;
	std::unique_ptr<VideoEncoder> encoder, external;
	std::unique_ptr<MediaWriter> media;
	std::unique_ptr<RecordingFile> journal;
	std::deque<CaptureFrame> frames;
	std::optional<CaptureFrame> latest, resize;
	std::int64_t origin = 0, segmentStart = 0, next = 0, stopped = 0, resizeSince = 0;
	std::int64_t nextSlot = 0;
	int observedWidth = 0, observedHeight = 0;
	std::int64_t checkpoint = 0;
	std::vector<Chapter> chapters;
	std::vector<std::pair<std::string, std::int64_t>> completed;
	bool done = false, forceSoftware = false;
	std::uint64_t inputDrops = 0, packets = 0, checkpointGeneration = 0;
	Impl(std::string path, Options options, SessionStorage storage,
		std::function<void(const Status &)> report, std::unique_ptr<VideoEncoder> external)
		: base(std::move(path)), options(options), storage(std::move(storage)), report(std::move(report)), external(std::move(external))
	{
		this->path = base; work = base+".recording/";
		status.path = base; status.state = State::Starting; status.segment = 1;
		this->report(status);
	}
	void line(const std::string &s)
	{
		if (!journal) return;
		if (journal->write(reinterpret_cast<const unsigned char *>(s.data()), int(s.size())) != int(s.size()))
			throw std::runtime_error("Cannot write recording journal");
		journal->flush();
	}
	void manifest(bool complete, std::int64_t duration)
	{
		std::ostringstream out;
		out << "{\"version\":1,\"video\":" << json(utf8(std::filesystem::u8path(path).filename()))
			<< ",\"session\":" << json(utf8(std::filesystem::u8path(base).filename()))
			<< ",\"segment\":" << status.segment << ",\"session_start_us\":" << segmentStart
			<< ",\"checkpoint_generation\":" << ++checkpointGeneration
			<< ",\"complete\":" << (complete ? "true" : "false") << ",\"error\":" << json(status.error)
			<< ",\"duration_us\":" << duration << ",\"fps\":" << options.fps
			<< ",\"width\":" << status.width << ",\"height\":" << status.height
			<< ",\"crf\":" << options.crf << ",\"encoder\":" << json(status.encoder)
			<< ",\"fallback_reason\":" << json(status.fallbackReason)
			<< ",\"video_codec\":\"h264\",\"audio_codec\":\"aac\",\"dropped_frames\":" << status.droppedFrames
			<< ",\"dropped_audio_samples\":" << status.droppedAudioSamples << ",\"chapters\":[";
		for (std::size_t i=0; i<chapters.size(); ++i)
		{
			const auto &c = chapters[i]; if (i) out << ',';
			out << "{\"id\":" << i+1 << ",\"title\":" << json(c.context.title(options.chapterTicks))
				<< ",\"start_us\":" << c.start << ",\"end_us\":" << (i+1 == chapters.size() ? duration : c.end) << ',' << c.context.fields(options.chapterTicks);
			if (c.context.match) out << ",\"start_tick\":" << c.context.tick << ",\"last_tick\":" << c.lastTick;
			out << '}';
		}
		out << "]}\n";
		text(storage,work+(checkpointGeneration%2 ? "manifest.1.json" : "manifest.2.json"),out.str());
		text(storage,work+"manifest.json",out.str());
	}
	void begin(CaptureFrame frame, std::int64_t relative)
	{
		if (!origin) origin = frame.time;
		segmentStart = relative;
		path = base;
		if (status.segment > 1)
		{
			auto p = std::filesystem::u8path(base); std::ostringstream suffix;
			suffix << ".part" << std::setw(4) << std::setfill('0') << status.segment;
			path = utf8(p.parent_path() / (utf8(p.stem()) + suffix.str() + ".mp4"));
			storage.reserve(path);
		}
		work = path + ".recording/";
		status.sourceWidth = frame.width; status.sourceHeight = frame.height;
		status.path = path; status.width = frame.width + frame.width%2; status.height = frame.height + frame.height%2;
		encoder = std::move(external);
		if (!encoder) encoder = storage.video({status.width,status.height,options.fps,options.crf,forceSoftware || options.encoder == EncoderPreference::Software});
		const auto &description = encoder->description();
		status.encoder = description.encoder;
		if (!description.fallbackReason.empty()) status.fallbackReason = description.fallbackReason;
		media = std::make_unique<MediaWriter>(work+"capture.mp4",description,storage.open);
		journal = storage.open(work+"events.jsonl",true);
		latest = std::move(frame); chapters.clear(); packets = 0;
		status.state = State::Recording; report(status);
		manifest(false,0);
	}
	void drain()
	{
		VideoPacket packet;
		while (encoder->receive(packet)) { media->video(packet); ++packets; }
	}
	bool finish(std::int64_t end, bool encoderFailed = false)
	{
		if (!media) return true;
		auto duration = std::max<std::int64_t>(1,end-segmentStart);
		if (!chapters.empty()) chapters.back().end = duration;
		if (!encoderFailed)
		{
			try { bool flushed = encoder->finish(); drain(); if (!flushed) return false; }
			catch (const std::exception &e)
			{
				if (status.encoder == "libx264") throw;
				status.fallbackReason = e.what(); forceSoftware = true; encoderFailed = true;
			}
		}
		if (encoderFailed && !packets)
		{
			media.reset(); encoder.reset(); journal.reset();
			auto error = status.error; status.error = status.fallbackReason;
			manifest(false,duration); status.error = error;
			return true;
		}
		media->finish(duration);
		media.reset(); encoder.reset();
		manifest(false,duration);
		std::vector<MediaChapter> navigation;
		for (const auto &c : chapters) navigation.push_back({c.start,c.end,c.context.title(options.chapterTicks)});
		MediaWriter::finalize(work+"capture.mp4",work+"final.mp4",navigation,storage.open);
		manifest(true,duration); journal.reset();
		storage.publish(work,path);
		completed.emplace_back(path,segmentStart); status.outputs.push_back(path);
		storage.cleanup(work);
		report(status);
		return true;
	}
	void index()
	{
		std::ostringstream out; out << "{\"version\":1,\"segments\":[";
		for (std::size_t i=0; i<completed.size(); ++i)
		{
			if (i) out << ',';
			out << "{\"video\":" << json(utf8(std::filesystem::u8path(completed[i].first).filename()))
				<< ",\"session_start_us\":" << completed[i].second << '}';
		}
		out << "]}\n";
		// This name was included in the session reservation's collision checks.
		text(storage,base+".session.json",out.str());
	}
};
Session::Session(std::string path, Options options, SessionStorage storage,
	std::function<void(const Status &)> report, std::unique_ptr<VideoEncoder> external)
	: impl(std::make_unique<Impl>(std::move(path),options,std::move(storage),std::move(report),std::move(external))) {}
Session::~Session() = default;
std::size_t Session::queuedFrames() const { return impl->frames.size(); }
void Session::frame(CaptureFrame frame)
{
	if (frame.width != impl->observedWidth || frame.height != impl->observedHeight)
	{
		impl->observedWidth = frame.width; impl->observedHeight = frame.height;
		impl->resizeSince = frame.time;
	}
	if (impl->frames.size() == 3) { ++impl->inputDrops; ++impl->status.droppedFrames; impl->report(impl->status); return; }
	if (frame.pixelLayout > 5) throw std::runtime_error("Unsupported capture pixel layout");
	const unsigned bytesPerPixel = frame.pixelLayout < 4 ? 4 : 3;
	if (frame.width <= 0 || frame.height <= 0 || frame.rgba.size() != std::size_t(frame.width)*frame.height*bytesPerPixel)
		throw std::runtime_error("Invalid capture buffer");
	if (frame.pixelLayout || frame.bottomUp)
	{
		static constexpr unsigned channels[][4] = {{0,1,2,3},{2,1,0,3},{1,2,3,0},{3,2,1,0},{0,1,2,0},{2,1,0,0}};
		const auto &channel = channels[frame.pixelLayout];
		std::vector<unsigned char> rgba(std::size_t(frame.width)*frame.height*4);
		for (int y=0;y<frame.height;++y)
		{
			const auto *source = frame.rgba.data()+std::size_t(frame.bottomUp ? frame.height-y-1 : y)*frame.width*bytesPerPixel;
			auto *target = rgba.data()+std::size_t(y)*frame.width*4;
			for (int x=0;x<frame.width;++x,source+=bytesPerPixel,target+=4)
			{ for (unsigned c=0;c<3;++c) target[c]=source[channel[c]]; target[3]=bytesPerPixel==3 ? 255 : source[channel[3]]; }
		}
		frame.rgba = std::move(rgba); frame.pixelLayout = 0; frame.bottomUp = false;
	}
	impl->frames.push_back(std::move(frame));
}
void Session::audio(const std::int16_t *samples, std::size_t count, std::int64_t time)
{
	if (!impl->media) return;
	impl->media->audio(samples,count/2,(time-impl->origin-impl->segmentStart)*44100/1000000);
}
void Session::event(std::int64_t time, const std::string &kind, const std::string &value)
{
	impl->line("{\"time_us\":"+std::to_string(std::max<std::int64_t>(0,time-impl->origin-impl->segmentStart))+
		",\"kind\":"+json(kind)+",\"value\":"+json(value)+"}\n");
}
void Session::drops(std::uint64_t frames, std::uint64_t audio)
{
	frames += impl->inputDrops;
	if (frames != impl->status.droppedFrames || audio != impl->status.droppedAudioSamples)
	{
		impl->line("{\"time_us\":"+std::to_string(impl->next-impl->segmentStart)+",\"kind\":\"capture_gap\",\"dropped_frames\":"+std::to_string(frames-impl->status.droppedFrames)+",\"dropped_audio_samples\":"+std::to_string(audio-impl->status.droppedAudioSamples)+"}\n");
		impl->status.droppedFrames = frames; impl->status.droppedAudioSamples = audio;
		impl->report(impl->status);
	}
}
void Session::stop(std::int64_t time) { impl->stopped = time; }
void Session::fail(const std::string &error)
{
	impl->status.error = error; impl->status.state = State::Failed;
	try { impl->manifest(false,std::max<std::int64_t>(0,impl->next-impl->segmentStart)); } catch (...) {}
	impl->report(impl->status); impl->done = true;
}
bool Session::step(std::int64_t time)
{
	auto &s = *impl;
	if (s.done) return true;
	if (!s.latest)
	{
		if (s.frames.empty())
		{
			if (s.stopped) throw std::runtime_error("Recording stopped before a frame was captured");
			return false;
		}
		auto frame = std::move(s.frames.front()); s.frames.pop_front(); s.begin(std::move(frame),0);
	}
	auto target = std::max<std::int64_t>(0,(s.stopped ? s.stopped : time-50000)-s.origin);
	if (s.next > target && !s.stopped) return false;
	if (target-s.next > 250000)
	{
		event(time,"background_gap",std::to_string(target-s.next));
		s.nextSlot = target*s.options.fps/1000000;
		s.next = s.nextSlot*1000000/s.options.fps;
	}
	while (!s.frames.empty() && s.frames.front().time <= s.origin+s.next)
	{
		auto frame = std::move(s.frames.front()); s.frames.pop_front();
		if (frame.width != s.latest->width || frame.height != s.latest->height)
		{
			s.resize = std::move(frame);
		}
		else { s.latest = std::move(frame); s.resize.reset(); }
	}
	if (s.resize && s.resize->width == s.observedWidth && s.resize->height == s.observedHeight && time-s.resizeSince >= 250000)
	{
		event(time,"resize",std::to_string(s.resize->width)+"x"+std::to_string(s.resize->height));
		if (!s.finish(s.next)) return false;
		++s.status.segment;
		auto frame = std::move(*s.resize); s.resize.reset(); s.begin(std::move(frame),s.next);
	}
	if (s.stopped && s.next >= target && !s.chapters.empty())
	{
		s.status.state = State::Finalizing; s.report(s.status);
		if (!s.finish(s.next)) return false;
		s.index(); s.status.state = State::Complete; s.report(s.status); s.done = true; return true;
	}
	const auto relative = s.next-s.segmentStart;
	if (!s.encoder->ready()) return false;
	if (s.chapters.empty() || s.chapters.back().context.key(s.options.chapterTicks) != s.latest->context.key(s.options.chapterTicks))
	{
		if (!s.chapters.empty()) s.chapters.back().end = relative;
		s.chapters.push_back({relative,relative,s.latest->context,s.latest->context.tick});
		s.line("{\"time_us\":"+std::to_string(relative)+",\"kind\":\"chapter\",\"id\":"+std::to_string(s.chapters.size())+","+s.latest->context.fields(s.options.chapterTicks)+"}\n");
	}
	s.chapters.back().lastTick = s.latest->context.tick;
	try
	{
		s.encoder->submit(s.latest->rgba.data(),s.latest->width,s.latest->height,relative); s.drain();
	}
	catch (const std::exception &e)
	{
		if (s.status.encoder == "libx264") throw;
		s.status.fallbackReason = e.what(); s.forceSoftware = true;
		s.finish(s.next,true); ++s.status.segment;
		auto frame = std::move(*s.latest); s.latest.reset(); s.begin(std::move(frame),s.next);
		return false;
	}
	// Leave room for device callbacks before filling muted/stopped audio with silence.
	if (relative > 250000) s.media->silenceThrough((relative-250000)*44100/1000000);
	s.next = (++s.nextSlot)*1000000/s.options.fps;
	if (s.next >= s.checkpoint) { s.media->checkpoint(); s.manifest(false,s.next-s.segmentStart); s.checkpoint = s.next+1000000; }
	return false;
}
} // namespace GAGCore::Recording::Detail
