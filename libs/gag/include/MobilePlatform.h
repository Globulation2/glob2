// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef __EMSCRIPTEN__
// platforms/web/canvas-size.js: 1 when the browser runs on a phone or tablet.
extern "C" int glob2_browser_mobile();
#endif

namespace GAGCore
{
//! Whether this process runs on a phone or tablet: the Android and iOS builds,
//! and the browser build on Android, iPhone or iPad. Phones share one memory
//! pool between CPU and GPU and kill a tab or app long before desktop limits,
//! so the render budget and DeviceProfile follow this, not the build alone.
inline bool platformIsMobile()
{
#if defined(GLOB2_MOBILE) || defined(__ANDROID__)
	return true;
#elif defined(__EMSCRIPTEN__)
	static const bool mobile = glob2_browser_mobile() != 0;
	return mobile;
#else
	return false;
#endif
}
} // namespace GAGCore
