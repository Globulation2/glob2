// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Which optional features this build contains (buildsys/edition.py). A browser
// edition such as `edition=lite` sets GLOB2_HAS_<FEATURE> to 0 in the generated
// glob2/BuildConfig.h and leaves the feature's sources out; every other build
// has them all. Test with `#if GLOB2_HAS_EDITOR` around code that names the
// feature's symbols, and with BuildFeatures::Editor elsewhere.
#include <glob2/BuildConfig.h>

#ifndef GLOB2_EDITION
#define GLOB2_EDITION "standard"
#endif
#ifndef GLOB2_HAS_EDITOR
#define GLOB2_HAS_EDITOR 1
#endif
#ifndef GLOB2_HAS_ONLINE
#define GLOB2_HAS_ONLINE 1
#endif
#ifndef GLOB2_HAS_LAN
#define GLOB2_HAS_LAN 1
#endif
#ifndef GLOB2_HAS_MCP
#define GLOB2_HAS_MCP 1
#endif
#ifndef GLOB2_HAS_HIVE
#define GLOB2_HAS_HIVE 1
#endif
#ifndef GLOB2_HAS_RECORDING
#define GLOB2_HAS_RECORDING 1
#endif
#ifndef GLOB2_HAS_VIDEO_ASSETS
#define GLOB2_HAS_VIDEO_ASSETS 1
#endif
#ifndef GLOB2_HAS_AI_MAXIMA
#define GLOB2_HAS_AI_MAXIMA 1
#endif
#ifndef GLOB2_HAS_RENDERER_GL
#define GLOB2_HAS_RENDERER_GL 1
#endif
#ifndef GLOB2_HAS_RENDERER_SOFTWARE
#define GLOB2_HAS_RENDERER_SOFTWARE 1
#endif
#ifndef GLOB2_HAS_RENDERER_WEBGPU
#define GLOB2_HAS_RENDERER_WEBGPU 1
#endif
#ifndef GLOB2_HAS_HD_ASSETS
#define GLOB2_HAS_HD_ASSETS 1
#endif
#ifndef GLOB2_HAS_MUSIC_SETS
#define GLOB2_HAS_MUSIC_SETS 1
#endif
#ifndef GLOB2_HAS_SKINS
#define GLOB2_HAS_SKINS 1
#endif
#ifndef GLOB2_HAS_TRANSLATIONS
#define GLOB2_HAS_TRANSLATIONS 1
#endif
#ifndef GLOB2_HAS_JS_TOOLKIT
#define GLOB2_HAS_JS_TOOLKIT 1
#endif
#ifndef GLOB2_HAS_CLI
#define GLOB2_HAS_CLI 1
#endif
#ifndef GLOB2_HAS_HARFBUZZ
#define GLOB2_HAS_HARFBUZZ 1
#endif
#ifndef GLOB2_HAS_SDL_EXTRAS
#define GLOB2_HAS_SDL_EXTRAS 1
#endif
#ifndef GLOB2_HAS_PREMADE_MAPS
#define GLOB2_HAS_PREMADE_MAPS 1
#endif
#ifndef GLOB2_HAS_UNUSED_FILES
#define GLOB2_HAS_UNUSED_FILES 1
#endif
#ifndef GLOB2_HAS_PAGE_FONT
#define GLOB2_HAS_PAGE_FONT 1
#endif

namespace BuildFeatures
{
inline constexpr const char *Edition = GLOB2_EDITION;
inline constexpr bool Editor = GLOB2_HAS_EDITOR;
inline constexpr bool Online = GLOB2_HAS_ONLINE;
inline constexpr bool LAN = GLOB2_HAS_LAN;
inline constexpr bool MCP = GLOB2_HAS_MCP;
inline constexpr bool Hive = GLOB2_HAS_HIVE;
inline constexpr bool Recording = GLOB2_HAS_RECORDING;
inline constexpr bool VideoAssets = GLOB2_HAS_VIDEO_ASSETS;
inline constexpr bool Maxima = GLOB2_HAS_AI_MAXIMA;
inline constexpr bool GLRenderer = GLOB2_HAS_RENDERER_GL;
inline constexpr bool SoftwareRenderer = GLOB2_HAS_RENDERER_SOFTWARE;
inline constexpr bool WebGPURenderer = GLOB2_HAS_RENDERER_WEBGPU;
inline constexpr bool HDArtwork = GLOB2_HAS_HD_ASSETS;
inline constexpr bool MusicSets = GLOB2_HAS_MUSIC_SETS;
inline constexpr bool Skins = GLOB2_HAS_SKINS;
inline constexpr bool Translations = GLOB2_HAS_TRANSLATIONS;
inline constexpr bool JavaScriptToolkit = GLOB2_HAS_JS_TOOLKIT;
inline constexpr bool CommandLineTools = GLOB2_HAS_CLI;
inline constexpr bool PremadeMaps = GLOB2_HAS_PREMADE_MAPS;
}
