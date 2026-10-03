// SPDX-License-Identifier: GPL-3.0-or-later
#include "OnlineServices.h"
#include "InstanceConfig.h"
#include "InviteLink.h"
#include "MapCache.h"
#include "OnlineStorage.h"
#include "PlatformClient.h"

#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif

#include <SDL3/SDL.h>
#include <memory>

#ifdef __ANDROID__
#include <jni.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include "mobile/ios/LaunchLinks.h"
#endif
#endif

namespace Online
{
namespace
{
struct Owned
{
	std::unique_ptr<OnlineStorage> storage;
	std::unique_ptr<InstanceConfig> config;
	std::unique_ptr<PlatformClient> client;
	std::unique_ptr<MapCache> maps;
	std::unique_ptr<Services> view;
};
Owned *owned = nullptr;
Uint64 lastPlatformPoll = 0;

#ifdef __ANDROID__
// Glob2Activity keeps the newest link it was opened with (onCreate,
// onNewIntent); take it from the game thread.
std::string takeAndroidLaunchLink()
{
	auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	if (!env)
		return {};
	auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
	if (!activity)
		return {};
	std::string link;
	auto cls = env->GetObjectClass(activity);
	auto method = env->GetStaticMethodID(cls, "takeLaunchLink", "()Ljava/lang/String;");
	if (method)
	{
		auto value = static_cast<jstring>(env->CallStaticObjectMethod(cls, method));
		if (value)
		{
			if (const char *chars = env->GetStringUTFChars(value, nullptr))
			{
				link = chars;
				env->ReleaseStringUTFChars(value, chars);
			}
			env->DeleteLocalRef(value);
		}
	}
	if (env->ExceptionCheck())
		env->ExceptionClear();
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(activity);
	return link;
}
#endif

void pollPlatformLinks()
{
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
	const Uint64 now = SDL_GetTicks();
	if (lastPlatformPoll && now - lastPlatformPoll < 250)
		return;
	lastPlatformPoll = now;
#ifdef __ANDROID__
	const auto link = takeAndroidLaunchLink();
#else
	const auto link = iosTakeLaunchLink();
#endif
	if (!link.empty())
		acceptInviteText(link);
#endif
}
} // namespace

Services &services()
{
	if (!owned)
	{
		auto created = std::make_unique<Owned>();
		created->storage = makeUserDirectoryStorage();
		created->config = std::make_unique<InstanceConfig>(*created->storage);
		created->config->load();
		created->client = std::make_unique<PlatformClient>(*created->config);
		created->maps = std::make_unique<MapCache>(*created->storage, HttpFetch::start);
		created->view = std::make_unique<Services>(
			Services{*created->storage, *created->config, *created->client, *created->maps});
		owned = created.release();
	}
	return *owned->view;
}

bool servicesCreated()
{
	return owned != nullptr;
}

void pump()
{
	pollPlatformLinks();
	if (owned)
		owned->client->update();
}

bool acceptDroppedText(const std::string &text)
{
	return acceptInviteText(text);
}

} // namespace Online
