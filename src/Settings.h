// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "Header.h"
#include "ExperimentalFeatures.h"
#include <string>
#include <map>
#include "IntBuildingType.h"
#include "BasePlayer.h" // for the MAX_NAME_LENGTH val.

class Settings
{
public:
	Settings();
	void load(const std::string filename="preferences.txt");
	// Checked atomic local replacement; callers separately await host persistence.
	bool save(const std::string filename="preferences.txt");

	/**
	 * Returns the username variable in settings.
	 * @return the username currently set.
	 */
	std::string getUsername();

	/**
	 * Sets the username in the settings object.
	 * The provided new string is controlled so that is not to long before
	 * the variable username is set with the new string.
	 * @param s The new username.
	 */
	void setUsername(std::string);


	/**
	 * all variables should really be private, we're working on it
	 * TODO: make all variables private
	 */
private:
	std::string username;

public:
	int screenWidth;
	int screenHeight;
	//! interface scale in percent; 0 follows the desktop
	int uiScale;
	Uint32 screenFlags;
	Uint32 optionFlags;
	bool automaticTorus; // Opt-in movement-triggered overview; local presentation only.
	std::string language;
	Uint32 musicVolume;
	Uint32 voiceVolume;
	int mute;
	int version;
	bool rememberUnit;
	bool scrollWheelEnabled;
	bool hiveMindEnabled;
	bool hiveMindSupervision;
	bool highResolutionArtwork;
	// Local rendering preferences; never serialized into games or orders.
	bool clouds;
	bool cloudShadows;
	bool buildingParticles;
	bool fullMagicEffects;
	bool translucentPanels;
	bool translucentPathLines;
	bool smoothProgressIndicators;
	bool decorativeAnimations;
	//! Experimental: draw units moving and animating between simulation ticks.
	//! Off draws exactly the simulated positions. Not part of setGraphicsDetail.
	bool unitInterpolation = false;
	void setGraphicsDetail(bool full);
	static constexpr Uint32 LEGACY_LOW_DETAIL = 0x1;
	/// Periodically saves the game in progress as "Auto save".
	bool autosaveGames;
	/// Experimental features to bake into every new game this player starts or
	/// hosts (Settings > Experiments). Saved as comma-separated keys; a key this
	/// build no longer knows is dropped on load.
	ExperimentSet experiments;
	/// Simulation speed preset. Zero is the original 25 ticks/second;
	/// higher values progressively reduce delays and then skip rendered frames.
	int gameSpeed;
    std::string interfacePresentation = "automatic";
    // Size of all touch interface text, 100..150 percent: menus, dialogs and the
    // HUD. Local UI preference; never part of saves/orders. Replaces the former
    // mobileDialogTextPercent, which only compensated in-game dialogs and is ignored.
    int textSizePercent = 100;
    // Store and apply the text size (clamped) to every touch text surface.
    void setTextSizePercent(int percent);
    // Touch scroll feel, 0..100 each: 0 turns the effect off, 50 is the default.
    // Local presentation preferences; never part of saves or orders.
    int touchScrollMomentum = 50; // lists, panels and trays keep moving after a flick
    int touchScrollBounce = 50;   // lists, panels and trays stretch past their ends
    int mapScrollMomentum = 50;   // game and editor maps keep panning after a flick
	/// One-finger zoom (double-tap, hold, drag) direction; local UI preference.
	enum OneFingerZoom
	{
		ONE_FINGER_ZOOM_PLATFORM = 0, ///< Match the system maps app
		ONE_FINGER_ZOOM_UP_IN = 1,
		ONE_FINGER_ZOOM_DOWN_IN = 2,
	};
	int oneFingerZoomDirection;
	/// Bottom corner the phone controls gather in; local UI preference.
	enum ThumbSide
	{
		THUMB_RIGHT = 0,
		THUMB_LEFT = 1,
	};
	int thumbSide;
	/// Resolves the platform default: Google Maps (Android) zooms in on a
	/// downward drag, Apple Maps and others on an upward drag.
	bool dragUpZoomsIn(void) const;

	enum
	{
		GAME_SPEED_MINIMUM = -3,
		GAME_SPEED_NORMAL = 0,
		GAME_SPEED_MAXIMUM = 10,
	};

	/// Milliseconds allotted to each simulation step for the selected preset.
	int getGameSpeedStepDuration(void) const;
	/// Number of simulation steps between rendered frames for the selected preset.
	int getGameSpeedRenderInterval(void) const;
	/// Human-readable multiplier used by settings, in-game options and notifications.
	std::string getGameSpeedText(void) const;
	/// Change presets while keeping the value within the supported range.
	void changeGameSpeed(int amount);

	

	///Levels are from 0 to 5, where even numbers are building
	///under construction and odd ones are completed buildings.
	int defaultUnitsAssigned[IntBuildingType::NB_BUILDING][6];
	///Default radius of flags, 0 for exploration, 1 for war flag, 2 for clearing flag
	int defaultFlagRadius[3];

	int cloudPatchSize;//the bigger the faster the uglier
	int cloudMaxAlpha;//the higher the nicer the clouds the harder the units are visible
	int cloudMaxSpeed;
	int cloudWindStability;//how much will the wind change
	int cloudStability;//how much will the clouds change shape
	int cloudSize;//the bigger the better they look with big Patches. The smaller the better they look with smaller patches
	int cloudHeight;//(cloud - ground) / (eyes - ground)

	int tempUnit;
	int tempUnitFuture;

	void resetDefaultUnitsAssigned();
	void resetDefaultFlagRadius();
};

//Version 1 - Resets default units assigned and keyboard shortcuts
#define SETTINGS_VERSION 1
