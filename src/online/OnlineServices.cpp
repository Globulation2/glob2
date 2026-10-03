// SPDX-License-Identifier: GPL-3.0-or-later
#include "OnlineServices.h"
#include "InstanceConfig.h"
#include "InviteLink.h"
#include "MapCache.h"
#include "OnlineStorage.h"
#include "PlatformClient.h"
#include "QuickMatch.h"
#include "RelayTransport.h"

#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif

#include <SDL3/SDL.h>
#include <algorithm>
#include <memory>
#include <vector>

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
// The user directory's online storage and map cache, shared by the platform
// client's services and by LAN games (which must not start the platform client).
struct Local
{
	std::unique_ptr<OnlineStorage> storage;
	std::unique_ptr<MapCache> maps;
};
Local *local = nullptr;
Local &localFiles()
{
	if (!local)
	{
		auto created = std::make_unique<Local>();
		created->storage = makeUserDirectoryStorage();
		created->maps = std::make_unique<MapCache>(*created->storage, HttpFetch::start);
		local = created.release();
	}
	return *local;
}
// Services for tools and harnesses that run without an Application (process lifetime).
Services *fallback = nullptr;
ServicesOwner *currentOwner = nullptr;
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

namespace
{
std::unique_ptr<InstanceConfig> loadedConfig(OnlineStorage &storage)
{
	auto config = std::make_unique<InstanceConfig>(storage);
	config->load();
	return config;
}
} // namespace

Services::Services(OnlineStorage &storage, MapCache &maps)
	: ownedConfig(loadedConfig(storage)), ownedClient(std::make_unique<PlatformClient>(*ownedConfig)),
	  storage(storage), config(*ownedConfig), client(*ownedClient), maps(maps)
{
}

// The search and the hooks go first (they use the client), then the client
// (closing its connection), then the instance list.
Services::~Services()
{
	hooks.clear();
	search.reset();
}

QuickMatch &Services::quickMatch()
{
	if (!search)
		search = std::make_unique<QuickMatch>(client, QuickMatch::Environment::native());
	return *search;
}

void Services::update()
{
	client.update();
	if (search)
		search->update();
	// A hook may add or remove hooks: iterate over a snapshot of the ids.
	std::vector<HookId> ids;
	ids.reserve(hooks.size());
	for (const auto &hook : hooks)
		ids.push_back(hook.first);
	for (HookId id : ids)
	{
		auto it = std::find_if(hooks.begin(), hooks.end(), [id](const auto &h) { return h.first == id; });
		if (it != hooks.end())
		{
			auto run = it->second;
			run();
		}
	}
}

Services::HookId Services::addHook(std::function<void()> hook)
{
	const HookId id = nextHook++;
	hooks.emplace_back(id, std::move(hook));
	return id;
}

void Services::removeHook(HookId id)
{
	hooks.erase(std::remove_if(hooks.begin(), hooks.end(), [id](const auto &h) { return h.first == id; }),
				hooks.end());
}

ServicesOwner::ServicesOwner(std::function<std::unique_ptr<Services>()> make)
	: make(std::move(make)), previous(currentOwner)
{
	currentOwner = this;
}

ServicesOwner::~ServicesOwner()
{
	owned.reset();
	if (currentOwner == this)
		currentOwner = previous;
}

Services &ServicesOwner::get()
{
	if (!owned)
	{
		if (make)
			owned = make();
		else
		{
			auto &files = localFiles();
			owned = std::make_unique<Services>(*files.storage, *files.maps);
		}
	}
	return *owned;
}

Services &services()
{
	if (currentOwner)
		return currentOwner->get();
	if (!fallback)
	{
		auto &files = localFiles();
		fallback = new Services(*files.storage, *files.maps);
	}
	return *fallback;
}

MapCache &sharedMapCache()
{
	return *localFiles().maps;
}

bool servicesCreated()
{
	return currentOwner ? currentOwner->created() : fallback != nullptr;
}

void pump()
{
	pollPlatformLinks();
	pumpLingeringRelayConnections();
	if (servicesCreated())
		services().update();
}

Services::HookId addPumpHook(std::function<void()> hook)
{
	return services().addHook(std::move(hook));
}

bool acceptDroppedText(const std::string &text)
{
	return acceptInviteText(text);
}

} // namespace Online
