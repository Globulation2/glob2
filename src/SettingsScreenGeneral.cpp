// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "SettingsScreen.h"
#include "ExperimentalFeatures.h"
#include "GlobalContainer.h"
#include "SoundMixer.h"
#include <InterfacePresentation.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <FormatableString.h>
#include <algorithm>
#include <cmath>
#include <iterator>

using namespace GAGCore;

void SettingsScreen::buildGeneral()
{
	auto &s = globalContainer->settings;
	if (current == Category::Display)
	{
		info(tr("Choose how the game looks on your screen."));
#ifndef GLOB2_MOBILE
		if (!touchLayout || globalContainer->gfx->isNativeDesktop())
		{
			// Mobile uses the OS-managed viewport and portable renderer.
			section("Display");
			choice("display.mode", "Window mode", "Choose a window or fill the screen.",
				   bool(s.screenFlags & GraphicContext::FULLSCREEN),
				   {tr("Windowed"), tr("Fullscreen")},
				   [this](int v)
				   {
					   changeDisplay(
						   [v](Settings &s)
						   {
							   if (v)
							   {
								   s.screenFlags |= GraphicContext::FULLSCREEN;
							   }
							   else
							   {
								   s.screenFlags &= ~GraphicContext::FULLSCREEN;
							   }
						   });
				   });

			{
				// 0 follows the desktop; the rest are the scales desktops actually offer.
				static const int percents[] = {0, 100, 125, 150, 175, 200, 250, 300};
				const float desktop = GraphicContext::querySystemUiScale();
				const int desktopPercent =
					int(std::lround(std::max(1.0f, desktop > 0.0f ? desktop : 1.0f) * 100));
				std::vector<std::string> labels;
				for (int p : percents)
					labels.push_back(p ? std::to_string(p) + " %"
									   : FormattableString(tr("Match the desktop %0"))
											 .arg(std::to_string(desktopPercent) + " %"));
				int selected = std::find(std::begin(percents), std::end(percents), s.uiScale) -
							   std::begin(percents);
				if (selected >= int(std::size(percents)))
					selected = 0;
				choice("display.uiscale", "Interface scale",
					   "Enlarge menus, text and the sidebar on a high-resolution screen.", selected,
					   labels,
					   [this](int v)
					   {
						   if (v >= 0 && v < int(std::size(percents)))
							   changeUiScale(percents[v]);
					   });
			}
			info(FormattableString(tr("Current display %0 %1 %2 %3"))
					 .arg(globalContainer->gfx->getDrawableW())
					 .arg(globalContainer->gfx->getDrawableH())
					 .arg((globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)
							  ? "OpenGL"
							  : tr("Software"))
					 .arg(tr(globalContainer->gfx->getOptionFlags() & GraphicContext::FULLSCREEN
								 ? "Fullscreen"
								 : "Windowed")));
			if (restartRequired())
				info(tr("Saved — restart required"));
			if (displayError)
				info(tr("Could not change display mode. The previous mode was restored."));
		}
#endif
		section("Artwork & effects");
		auto effect = [this, &s](const char *id, const char *label, const char *help,
								 bool Settings::*field)
		{
			toggle(id, label, help, s.*field, [this, field](int v)
			{
				globalContainer->settings.*field = v != 0;
				commit();
			});
		};
		auto appearance = [this, &s](const char *id, const char *label, const char *help,
									 bool Settings::*field, const char *off, const char *on)
		{
			choice(id, label, help, s.*field, {tr(off), tr(on)}, [this, field](int v)
			{
				globalContainer->settings.*field = v != 0;
				commit();
			});
		};
		effect("graphics.clouds", "Clouds", "Show cloud cover above the map.", &Settings::clouds);
		effect("graphics.shadows", "Cloud shadows", "Show cloud shadows on the ground independently of cloud cover.", &Settings::cloudShadows);
		effect("graphics.particles", "Building particles", "Show smoke and other building particles.", &Settings::buildingParticles);
		appearance("graphics.magic", "Magic effects", "Choose animated magic effects or a simple visible effect.", &Settings::fullMagicEffects, "Simple", "Full");
#ifndef GLOB2_MOBILE
		if (!touchLayout || globalContainer->gfx->isNativeDesktop())
		{
			choice("graphics.artwork", "Artwork",
				   "Apply artwork on the next game or editor load (OpenGL).",
				   s.highResolutionArtwork, {tr("Original"), tr("High resolution")},
				   [this](int v)
				   {
					   globalContainer->settings.highResolutionArtwork = v;
					   commit();
				   });
			const bool gpu = bool(globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU);
			form.back().enabled = gpu;
			if (!gpu) form.back().help = tr("Requires the OpenGL renderer. Your saved choice is retained.");
			toggle("graphics.torus", "Automatic torus view",
				   "Automatically show the torus overview while moving around the map (OpenGL).",
				   s.automaticTorus,
				   [this](int v)
				   {
					   globalContainer->settings.automaticTorus = v;
					   commit();
				   });
			form.back().enabled = gpu;
			if (!gpu) form.back().help = tr("Requires the OpenGL renderer. Your saved choice is retained.");
		}
#endif
		section("Interface appearance");
		appearance("graphics.panels", "Panels", "Choose translucent or opaque interface panels.", &Settings::translucentPanels, "Opaque", "Translucent");
		choice("display.textsize", "Text size", "Enlarge interface text without changing the map scale.",
			   s.textSizePercent >= 150 ? 2 : s.textSizePercent >= 125 ? 1 : 0,
			   {"100 %", "125 %", "150 %"}, [this](int v)
			   {
				   // Menus, dialogs and the touch HUD all follow it on their next frame.
				   globalContainer->settings.setTextSizePercent(100 + v * 25);
				   commit();
			   });
		choice("display.presentation", "Interface layout",
			   "Spacious uses a side panel when there is room. Compact uses a toolbar and drawers.",
			   int(parsePresentationPreference(s.interfacePresentation)),
			   {tr("Automatic"), tr("Compact"), tr("Spacious")},
			   [this](int v)
			   {
				   presentationPreference =
					   static_cast<PresentationPreference>(std::clamp(v, 0, 2));
				   globalContainer->settings.interfacePresentation =
					   presentationPreferenceName(presentationPreference);
				   commit();
			   });
		section("Advanced graphics");
		appearance("graphics.paths", "Path lines", "Choose translucent or opaque unit path lines.", &Settings::translucentPathLines, "Opaque", "Translucent");
		effect("graphics.indicators", "Smooth progress indicators", "Smooth the moving edges of progress indicators.", &Settings::smoothProgressIndicators);
		effect("graphics.animation", "Decorative interface animation", "Animate victory artwork. Reduced motion also disables this animation.", &Settings::decorativeAnimations);
#ifndef GLOB2_MOBILE
		if (!touchLayout || globalContainer->gfx->isNativeDesktop())
		{
			choice("graphics.renderer", "Renderer", "Changing the renderer requires a restart.",
				   bool(s.screenFlags & GraphicContext::USEGPU), {tr("Software"), "OpenGL"},
				   [this](int v)
				   {
					   changeDisplay(
						   [v](Settings &s)
						   {
							   if (v)
								   s.screenFlags |= GraphicContext::USEGPU;
							   else
								   s.screenFlags &= ~GraphicContext::USEGPU;
						   });
				   });
#ifndef HAVE_OPENGL
			form.back().enabled = false;
			form.back().help = tr("OpenGL is not available in this build.");
#endif
		}
#endif
		if (touchLayout)
			choice("display.zoomdrag", "One-finger zoom", "Double-tap the map, hold, then drag to zoom.",
				   s.oneFingerZoomDirection,
				   {tr("Platform default"), tr("Drag up zooms in"), tr("Drag down zooms in")},
				   [this](int v)
				   {
					   globalContainer->settings.oneFingerZoomDirection =
						   std::clamp(v, int(Settings::ONE_FINGER_ZOOM_PLATFORM),
									  int(Settings::ONE_FINGER_ZOOM_DOWN_IN));
					   commit();
				   });
		if (touchLayout)
			choice("display.thumb", "Thumb side", "Phone controls gather in this bottom corner.",
				   s.thumbSide, {tr("Right"), tr("Left")},
				   [this](int v)
				   {
					   globalContainer->settings.thumbSide =
						   std::clamp(v, int(Settings::THUMB_RIGHT), int(Settings::THUMB_LEFT));
					   commit();
				   });
	}
	else if (current == Category::Audio)
	{
		info(tr("Adjust music and voice volume."));
		toggle("audio.mute", "Mute audio", "Keep your volume levels while silencing audio.", s.mute,
			   [this](int v)
			   {
				   auto &s = globalContainer->settings;
				   s.mute = v;
				   globalContainer->mix->setVolume(s.musicVolume, s.voiceVolume, s.mute);
				   commit();
			   });
		for (int voice = 0; voice < 2; ++voice)
		{
			auto &r = add(voice ? "audio.voice" : "audio.music", Kind::Slider,
						  tr(voice ? "Voice volume" : "Music volume"));
			r.number = voice ? s.voiceVolume : s.musicVolume;
			r.maximum = 256;
			r.enabled = !s.mute;
			r.value = std::to_string((r.number * 100 + 128) / 256) + "%";
			r.change = [this, voice](int v)
			{
				auto &s = globalContainer->settings;
				(voice ? s.voiceVolume : s.musicVolume) = std::clamp(v, 0, 256);
				globalContainer->mix->setVolume(s.musicVolume, s.voiceVolume, s.mute);
				commit(true);
			};
		}
	}
	else if (current == Category::Gameplay)
	{
		info(tr("Adjust the pace of play."));
		std::vector<std::string> labels;
		Settings copy = s;
		for (int i = Settings::GAME_SPEED_MINIMUM; i <= Settings::GAME_SPEED_MAXIMUM; ++i)
		{
			copy.gameSpeed = i;
			labels.push_back(copy.getGameSpeedText());
		}
		choice("gameplay.speed", "Game speed",
			   "Single-player and replays only. Multiplayer runs at 1x.",
			   s.gameSpeed - Settings::GAME_SPEED_MINIMUM, labels,
			   [this](int v)
			   {
				   globalContainer->settings.gameSpeed =
					   std::clamp(v + int(Settings::GAME_SPEED_MINIMUM),
						int(Settings::GAME_SPEED_MINIMUM), int(Settings::GAME_SPEED_MAXIMUM));
				   commit();
			   });
		toggle("gameplay.autosave", "Autosave", "Save the game automatically about every minute.",
			   s.autosaveGames,
			   [this](int v)
			   {
				   globalContainer->settings.autosaveGames = v;
				   commit();
			   });
	}
	else if (current == Category::Player)
	{
		info(tr("Set your language and player name."));
		auto *strings = Toolkit::getStringTable();
		std::vector<std::string> labels;
		for (int i = 0; i < strings->getNumberOfLanguage(); ++i)
		{
			auto label = strings->getStringInLang(
				strings->isLangComplete(i) ? "[language]" : "[language incomplete]", i);
			if (!Toolkit::getFont("standard")->hasGlyphsFor(label))
				label = strings->getStringInLang("[language-code]", i) + " — " + tr("Missing font");
			labels.push_back(label);
		}
		choice("player.language", "Language", "Language used throughout the interface.",
			   strings->getLang(), labels,
			   [this](int v)
			   {
				   auto *strings = Toolkit::getStringTable();
				   strings->setLang(v);
				   globalContainer->settings.language =
					   strings->getStringInLang("[language-code]", v);
				   commit();
			   });
		auto &r =
			add("player.name", Kind::Text, tr("Player name"), tr("Name shown to other players."));
		r.value = s.getUsername();
	}
	else if (current == Category::Experiments)
	{
		info(tr("Try features we are still testing. They can change balance and pacing."));
		info(tr("Experiments apply to new games you start or host, never to campaign missions. A saved game keeps the ones it started with."));
		if (experimentDefinitions().empty())
			info(tr("No experiments in this build."));
		auto *strings = Toolkit::getStringTable();
		for (const auto &definition : experimentDefinitions())
		{
			// Labels come from the experiment's own keys rather than the
			// "[settings ...]" prefix, so the lobby and this page share them.
			const std::string key = definition.key;
			auto &r = add("experiments." + key, Kind::Toggle, strings->getString("[experiment " + key + "]"),
						  strings->getString("[experiment " + key + " help]"));
			r.number = s.experiments.has(definition.id);
			r.change = [this, id = definition.id](int v)
			{
				globalContainer->settings.experiments.set(id, v != 0);
				commit();
			};
		}
	}
}
