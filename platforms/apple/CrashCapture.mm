// SPDX-License-Identifier: GPL-3.0-or-later
//
// iOS crash capture (src/app/CrashCapture.h) with MetricKit, which is built into
// iOS: after the game crashes, iOS delivers an MXCrashDiagnostic to the next
// run, some time after launch. Each one is written to reports/crashes/ as its
// own JSON (MXCrashDiagnostic.JSONRepresentation, unchanged), which the crash
// worker symbolicates against the build's dSYM. Compiled with ARC.
#include "CrashCapture.h"
#import <Foundation/Foundation.h>
#import <MetricKit/MetricKit.h>
#include <TargetConditionals.h>
#include <filesystem>
#include <string>

@interface Glob2CrashSubscriber : NSObject <MXMetricManagerSubscriber>
- (instancetype)initWithDirectory:(const std::string &)directory;
@end

@implementation Glob2CrashSubscriber
{
	std::string userDir;
}

- (instancetype)initWithDirectory:(const std::string &)directory
{
	if ((self = [super init]))
		userDir = directory;
	return self;
}

// Called on a MetricKit queue, not the game's thread.
- (void)didReceiveDiagnosticPayloads:(NSArray<MXDiagnosticPayload *> *)payloads
{
	bool stored = false;
	for (MXDiagnosticPayload *payload in payloads)
		for (MXCrashDiagnostic *crash in payload.crashDiagnostics)
		{
			NSData *json = [crash JSONRepresentation];
			if (json.length)
				stored |= CrashCapture::store(std::filesystem::u8path(userDir), CrashCapture::Kind::MetricKit,
											  std::string(static_cast<const char *>(json.bytes), json.length));
		}
	if (stored)
		CrashCapture::noteArrival();
}

#if !TARGET_OS_OSX
// Metrics payloads are not used. MXMetricPayload is unavailable on macOS,
// where this optional method must not be declared at all.
- (void)didReceiveMetricPayloads:(NSArray<MXMetricPayload *> *)payloads
{
}
#endif
@end

namespace
{
Glob2CrashSubscriber *subscriber = nil;
}

extern "C" bool glob2_crash_capture_built(void)
{
	return true;
}

extern "C" void glob2_crash_capture_start(const char *userDir)
{
	if (subscriber)
		return;
	subscriber = [[Glob2CrashSubscriber alloc] initWithDirectory:userDir];
	[[MXMetricManager sharedManager] addSubscriber:subscriber];
}
