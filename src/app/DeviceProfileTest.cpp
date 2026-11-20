// SPDX-License-Identifier: GPL-3.0-or-later
// Device classification, Auto values, the OS memory-signal responses and the
// session step-down trigger. Pure logic: no renderer, settings or JNI.
#include "Glob2Test.h"
#include "DeviceAdaptation.h"
#include "DeviceProfile.h"

#include <string>
#include <vector>

using namespace DeviceAdaptation;

namespace
{
DeviceInputs inputs(std::uint64_t ramMiB, unsigned cores = 8, bool mobile = false)
{
	DeviceInputs i;
	i.systemRamMiB = ramMiB;
	i.cores = cores;
	i.mobile = mobile;
	return i;
}

struct Fake
{
	bool autoOn = true;
	int stepDowns = 0;
	std::vector<Trim> trims;
	std::vector<Event> events;
	Controller controller(bool androidLevels, HitchWindow::Config config = HitchWindow::defaults())
	{
		return Controller({.artworkAutoOn = [this] { return autoOn; },
						   .stepDownArtwork = [this] { ++stepDowns; autoOn = false; },
						   .trim = [this](Trim t) { trims.push_back(t); },
						   .record = [this](const Event &e) { events.push_back(e); }},
						  androidLevels, config);
	}
};

constexpr std::uint64_t ms = 1'000'000;
} // namespace

TEST_SUITE("DeviceProfile")
{
	TEST_CASE("inputs classify into tiers by the threshold table")
	{
		struct Row
		{
			DeviceInputs in;
			DeviceTier tier;
			DeviceReason reason;
		};
		DeviceInputs lowRam = inputs(4096, 8, true);
		lowRam.androidLowRam = true;
		DeviceInputs androidTotal = inputs(8192, 8, true);
		androidTotal.androidTotalMemMiB = 2800; // totalMem wins over SDL's figure
		const Row rows[] = {
			{inputs(2765, 8, true), DeviceTier::Low, DeviceReason::Memory},   // "3 GB" phone
			{inputs(3071), DeviceTier::Low, DeviceReason::Memory},
			{inputs(3072), DeviceTier::Mid, DeviceReason::Memory},
			{inputs(3686, 8, true), DeviceTier::Mid, DeviceReason::Memory},   // "4 GB" phone
			{inputs(6144), DeviceTier::Mid, DeviceReason::Memory},
			{inputs(6145), DeviceTier::High, DeviceReason::Memory},
			{inputs(15800), DeviceTier::High, DeviceReason::Memory},
			{inputs(16384, 2), DeviceTier::Low, DeviceReason::Cores},
			{inputs(16384, 0), DeviceTier::High, DeviceReason::Memory},       // cores unknown
			{inputs(0), DeviceTier::High, DeviceReason::Unknown},             // nothing reported
			{inputs(0, 8, true), DeviceTier::Mid, DeviceReason::Unknown},     // Safari on a phone
			{inputs(0, 2, true), DeviceTier::Low, DeviceReason::Cores},
			{lowRam, DeviceTier::Low, DeviceReason::AndroidLowRam},
			{androidTotal, DeviceTier::Low, DeviceReason::Memory},
		};
		for (const auto &row : rows)
		{
			CAPTURE(row.in.systemRamMiB);
			CAPTURE(row.in.cores);
			const auto p = DeviceProfile::classify(row.in);
			CHECK(p.tier == row.tier);
			CHECK(p.reason == row.reason);
			CHECK(p.overrideSource.empty());
		}
		CHECK(DeviceProfile::classify(androidTotal).reasonValue == 2800);
	}

	TEST_CASE("an override decides the tier and parses only the three names")
	{
		CHECK(DeviceProfile::parseTier("low") == DeviceTier::Low);
		CHECK(DeviceProfile::parseTier("mid") == DeviceTier::Mid);
		CHECK(DeviceProfile::parseTier("high") == DeviceTier::High);
		CHECK_FALSE(DeviceProfile::parseTier("Low"));
		CHECK_FALSE(DeviceProfile::parseTier(""));
		const auto p = DeviceProfile::classify(inputs(32768), DeviceTier::Low, "environment");
		CHECK(p.tier == DeviceTier::Low);
		CHECK(p.reason == DeviceReason::Override);
		CHECK(p.overrideSource == "environment");
		CHECK_FALSE(p.highResolutionArtwork().on);
		CHECK(DeviceProfile::classify(inputs(1024), DeviceTier::High, "command-line").highResolutionArtwork().on);
	}

	TEST_CASE("Auto artwork and map size follow the tier; VRAM only for a discrete desktop GPU")
	{
		const auto low = DeviceProfile::classify(inputs(2048, 8, true));
		CHECK_FALSE(low.highResolutionArtwork().on);
		CHECK(low.highResolutionArtwork().reason == DeviceReason::Memory);
		CHECK(low.highResolutionArtwork().value == 2048);
		CHECK(low.newMapSize().shift == 7);

		CHECK(DeviceProfile::classify(inputs(4096, 8, true)).highResolutionArtwork().on);
		CHECK(DeviceProfile::classify(inputs(4096, 8, true)).newMapSize().shift == 7); // mid phone
		CHECK(DeviceProfile::classify(inputs(4096, 8, false)).newMapSize().shift == 8); // mid desktop
		CHECK(DeviceProfile::classify(inputs(16384, 8, true)).newMapSize().shift == 8);
		CHECK(DeviceProfile::classify(inputs(16384)).highResolutionArtwork().on);
		CHECK(DeviceProfile::classify(inputs(0)).newMapSize().shift == 8);
		// A phone browser that reports no memory: artwork on, the phone map size.
		CHECK(DeviceProfile::classify(inputs(0, 8, true)).highResolutionArtwork().on);
		CHECK(DeviceProfile::classify(inputs(0, 8, true)).newMapSize().shift == 7);
		// Too few cores lower the map size (simulation cost) but not artwork (memory).
		CHECK(DeviceProfile::classify(inputs(16384, 2)).newMapSize().shift == 7);
		CHECK(DeviceProfile::classify(inputs(16384, 2)).highResolutionArtwork().on);

		auto desktop = inputs(16384);
		desktop.vramMiB = 512;
		desktop.vramSource = "vulkan";
		const auto small = DeviceProfile::classify(desktop).highResolutionArtwork();
		CHECK_FALSE(small.on);
		CHECK(small.reason == DeviceReason::Vram);
		CHECK(small.value == 512);
		CHECK(DeviceProfile::classify(desktop).newMapSize().shift == 8); // VRAM is artwork-only
		desktop.vramMiB = 4096;
		CHECK(DeviceProfile::classify(desktop).highResolutionArtwork().on);
		auto phone = inputs(8192, 8, true);
		phone.vramMiB = 512;
		phone.vramSource = "vulkan";
		CHECK(DeviceProfile::classify(phone).highResolutionArtwork().on); // shared memory: no VRAM rule
		auto integrated = inputs(16384);
		integrated.vramSource = "integrated";
		CHECK(DeviceProfile::classify(integrated).highResolutionArtwork().on);
	}

	TEST_CASE("telemetry fields record the tier and every input")
	{
		auto in = inputs(2765, 8, true);
		in.androidLowRam = false;
		in.androidMemoryClassMiB = 256;
		in.androidLargeMemoryClassMiB = 512;
		const auto fields = DeviceProfile::classify(in).telemetryFields();
		for (const char *expected : {"tier=low", "source=detected", "reason=memory", "memory_mib=2765", "cores=8",
									 "mobile=1", "android_low_ram=0", "android_memory_class_mib=256",
									 "android_large_memory_class_mib=512", "auto_artwork=original", "auto_map_size=128"})
		{
			CAPTURE(expected);
			CHECK(fields.find(expected) != std::string::npos);
		}
	}
}

TEST_SUITE("DeviceAdaptation")
{
	TEST_CASE("onTrimMemory levels map to signals")
	{
		CHECK_FALSE(androidTrimSignal(0));
		CHECK(androidTrimSignal(5) == MemorySignal::AndroidRunningModerate);
		CHECK(androidTrimSignal(10) == MemorySignal::AndroidRunningLow);
		CHECK(androidTrimSignal(15) == MemorySignal::AndroidRunningCritical);
		CHECK(androidTrimSignal(20) == MemorySignal::AndroidUiHidden);
		CHECK(androidTrimSignal(40) == MemorySignal::AndroidBackground);
		CHECK(androidTrimSignal(60) == MemorySignal::AndroidBackground);
		CHECK(androidTrimSignal(80) == MemorySignal::AndroidBackground);
	}

	TEST_CASE("each memory signal gets its graded response")
	{
		struct Row
		{
			MemorySignal signal;
			bool android;
			Trim trim;
			bool stepDown;
		};
		const Row rows[] = {
			{MemorySignal::SdlLowMemory, false, Trim::Caches, true}, // iOS memory warning: at once
			{MemorySignal::SdlLowMemory, true, Trim::None, false},   // Android: the level decides
			{MemorySignal::AndroidRunningModerate, true, Trim::Caches, false},
			{MemorySignal::AndroidRunningLow, true, Trim::Caches, true},
			{MemorySignal::AndroidRunningCritical, true, Trim::Smallest, true},
			{MemorySignal::AndroidLowMemory, true, Trim::Smallest, true},
			{MemorySignal::AndroidUiHidden, true, Trim::Background, false},
			{MemorySignal::AndroidBackground, true, Trim::Background, false},
			{MemorySignal::BrowserHeapHigh, false, Trim::Smallest, true},
		};
		for (const auto &row : rows)
		{
			CAPTURE(signalName(row.signal));
			CAPTURE(row.android);
			Fake fake;
			auto controller = fake.controller(row.android);
			controller.signal(row.signal);
			CHECK(respond(row.signal, row.android).trim == row.trim);
			CHECK(fake.stepDowns == int(row.stepDown));
			CHECK(controller.steppedDown() == row.stepDown);
			CHECK(fake.trims == (row.trim == Trim::None ? std::vector<Trim>{} : std::vector<Trim>{row.trim}));
			REQUIRE(fake.events.size() == 1);
			CHECK(fake.events[0].cause == Cause::Memory);
			CHECK(fake.events[0].signal == row.signal); // telemetry names the OS signal
			CHECK(fake.events[0].steppedDown == row.stepDown);
			CHECK(controller.signals(row.signal) == 1);
			if (row.stepDown)
				CHECK(controller.stepDownEvent()->signal == row.signal);
		}
	}

	TEST_CASE("an explicit artwork choice is never stepped down, but caches are still trimmed")
	{
		Fake fake;
		fake.autoOn = false; // Off or On chosen, or Auto already off
		auto controller = fake.controller(true);
		controller.signal(MemorySignal::AndroidRunningCritical);
		controller.signal(MemorySignal::AndroidRunningLow);
		CHECK(fake.stepDowns == 0);
		CHECK_FALSE(controller.steppedDown());
		CHECK_FALSE(controller.takeNotice());
		CHECK(fake.trims == std::vector<Trim>{Trim::Smallest, Trim::Caches});
	}

	TEST_CASE("a step-down happens once and is offered to the player once")
	{
		Fake fake;
		auto controller = fake.controller(false);
		controller.signal(MemorySignal::SdlLowMemory);
		fake.autoOn = true; // even if something turned it back on, the session stays down
		controller.signal(MemorySignal::SdlLowMemory);
		CHECK(fake.stepDowns == 1);
		CHECK(controller.takeNotice());
		CHECK_FALSE(controller.takeNotice());
		CHECK(fake.trims.size() == 2);
	}

	TEST_CASE("sustained hitches after the grace period step Auto artwork down")
	{
		const HitchWindow::Config config{50 * ms, 1000 * ms, 500 * ms, 1000 * ms, 5};
		Fake fake;
		auto controller = fake.controller(false, config);
		std::uint64_t now = 1000 * ms;
		controller.matchStarted(now);
		// Loading hitches inside the grace period never count.
		for (int i = 0; i < 6; ++i)
			controller.presented(now += 80 * ms);
		CHECK(fake.stepDowns == 0);
		// Smooth frames, and gaps (pause, background) longer than gapNs, never count.
		for (int i = 0; i < 100; ++i)
			controller.presented(now += 16 * ms);
		controller.presented(now += 5000 * ms);
		CHECK(fake.events.empty());
		// Four slow frames are not yet enough; the fifth within the window is.
		for (int i = 0; i < 4; ++i)
			controller.presented(now += 60 * ms);
		CHECK(fake.stepDowns == 0);
		controller.presented(now += 60 * ms);
		CHECK(fake.stepDowns == 1);
		REQUIRE(fake.events.size() == 1);
		CHECK(fake.events[0].cause == Cause::Hitches);
		CHECK(controller.stepDownEvent()->cause == Cause::Hitches);
	}

	TEST_CASE("slow frames spread wider than the window do not trigger")
	{
		const HitchWindow::Config config{50 * ms, 1000 * ms, 0, 1000 * ms, 3};
		HitchWindow window(config);
		std::uint64_t now = 1;
		window.restart(now);
		int triggers = 0;
		for (int i = 0; i < 30; ++i)
		{
			triggers += window.presented(now += 60 * ms);
			for (int j = 0; j < 40; ++j)
				triggers += window.presented(now += 16 * ms); // ~700 ms between slow frames
		}
		CHECK(triggers == 0);
		// Three slow frames close together do; the ring then starts afresh.
		for (int i = 0; i < 3; ++i)
			triggers += window.presented(now += 60 * ms);
		CHECK(triggers == 1);
		CHECK_FALSE(window.presented(now += 60 * ms));
	}

	TEST_CASE("Android trims queue lock-free and are taken most severe first")
	{
		CHECK(takeQueuedSignals().empty());
		queueAndroidTrim(5);
		queueAndroidTrim(20);
		queueAndroidTrim(15);
		queueAndroidTrim(15);
		queueAndroidTrim(1); // unknown, below every level: ignored
		const auto taken = takeQueuedSignals();
		CHECK(taken == std::vector<MemorySignal>{MemorySignal::AndroidRunningCritical,
												 MemorySignal::AndroidRunningModerate, MemorySignal::AndroidUiHidden});
		CHECK(takeQueuedSignals().empty());
		queueAndroidLowMemory();
		CHECK(takeQueuedSignals() == std::vector<MemorySignal>{MemorySignal::AndroidLowMemory});
	}
}
