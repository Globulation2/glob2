// SPDX-License-Identifier: GPL-3.0-or-later
//
// Google Play Age Signals, the platform age signal of the Google Play build
// (src/app/AgeGate.h). Glob2Activity.requestAgeSignals asks the optional
// PlayAgeSignals class (platforms/mobile/android-play/, present only in the
// Play build) and answers through ageSignalAnswered with the same codes as the
// Apple shim: 0 unavailable, 1 declined, 2 under 13, 3 aged 13 or over. The
// F-Droid and Amazon builds have no such class and answer 0 at once.
#include <SDL3/SDL_system.h>
#include <jni.h>
#include <mutex>

namespace
{
std::mutex pendingMutex;
void (*pending)(int) = nullptr;

void answer(int code)
{
	void (*done)(int) = nullptr;
	{
		std::lock_guard<std::mutex> lock(pendingMutex);
		done = pending;
		pending = nullptr;
	}
	if (done)
		done(code);
}
}

// Overrides AgeGate.cpp's weak "unavailable" default.
extern "C" void glob2_request_age_range(void (*done)(int))
{
	{
		std::lock_guard<std::mutex> lock(pendingMutex);
		pending = done;
	}
	auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	auto activity = env ? static_cast<jobject>(SDL_GetAndroidActivity()) : nullptr;
	jclass type = activity ? env->GetObjectClass(activity) : nullptr;
	jmethodID method = type ? env->GetStaticMethodID(type, "requestAgeSignals", "(Landroid/app/Activity;)Z") : nullptr;
	const bool started = method && env->CallStaticBooleanMethod(type, method, activity) && !env->ExceptionCheck();
	if (env && env->ExceptionCheck())
		env->ExceptionClear();
	if (type)
		env->DeleteLocalRef(type);
	if (activity)
		env->DeleteLocalRef(activity);
	if (!started)
		answer(0);
}

extern "C" JNIEXPORT void JNICALL Java_org_globulation2_glob2_Glob2Activity_ageSignalAnswered(JNIEnv *, jclass, jint code)
{
	answer(static_cast<int>(code));
}
