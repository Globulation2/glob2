// SPDX-License-Identifier: GPL-3.0-or-later
#import <Foundation/Foundation.h>

extern "C" void* glob2BeginTestActivity()
{
    @autoreleasepool
    {
        id token = [[NSProcessInfo processInfo]
            beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
            reason:@"Running Globulation 2 native tests"];
        return [token retain];
    }
}

extern "C" void glob2EndTestActivity(void* opaque)
{
    @autoreleasepool
    {
        id token = static_cast<id>(opaque);
        [[NSProcessInfo processInfo] endActivity:token];
        [token release];
    }
}
