// SPDX-License-Identifier: GPL-3.0-or-later
#include "LaunchLinks.h"
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include <mutex>

namespace
{
std::mutex linkMutex;
std::string pendingLink;

BOOL continueUserActivity(id, SEL, UIApplication *, NSUserActivity *activity, id)
{
    if (![activity.activityType isEqualToString:NSUserActivityTypeBrowsingWeb] || !activity.webpageURL)
        return NO;
    const char *text = activity.webpageURL.absoluteString.UTF8String;
    if (!text)
        return NO;
    std::lock_guard<std::mutex> lock(linkMutex);
    pendingLink = text;
    return YES;
}
}

// SDL's UIKit delegate (SDLUIKitDelegate) does not implement universal-link
// continuation. Add it at load time without subclassing the delegate, leaving
// any implementation a newer SDL may bring untouched.
@interface Glob2LaunchLinks : NSObject
@end
@implementation Glob2LaunchLinks
+ (void)load
{
    Class delegate = NSClassFromString(@"SDLUIKitDelegate");
    SEL selector = @selector(application:continueUserActivity:restorationHandler:);
    if (!delegate || class_getInstanceMethod(delegate, selector))
        return;
    struct objc_method_description description =
        protocol_getMethodDescription(@protocol(UIApplicationDelegate), selector, NO, YES);
    if (description.types)
        class_addMethod(delegate, selector, (IMP)continueUserActivity, description.types);
}
@end

std::string iosTakeLaunchLink()
{
    std::lock_guard<std::mutex> lock(linkMutex);
    std::string link;
    link.swap(pendingLink);
    return link;
}
