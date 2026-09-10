// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "Header.h"
#include <string>
#include <map>
#include "IntBuildingType.h"
#include "BasePlayer.h" // for the MAX_NAME_LENGTH val.

class Settings
{
public:
	Settings();
	void load(const std::string filename="preferences.txt");
	void save(const std::string filename="preferences.txt");

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
	 * Sets the password in the Settings object.
	 * Provided an arbitrary string the password in the settings object is set
	 * to the given value.
	 * @param s The new password to use.
	 */
	std::string getPasswd();

	/**
	 * Returns the current password held in the Settings object.
	 * @return the currently held password.
	 */
	void setPasswd(std::string);


	/**
	 * all variables should really be private, we're working on it
	 * TODO: make all variables private
	 */
private:
	std::string username;
	std::string password;

public:
	int screenWidth;
	int screenHeight;
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
	bool highResolutionArtwork;
	/// Simulation speed preset. Zero is the original 25 ticks/second;
	/// higher values progressively reduce delays and then skip rendered frames.
	int gameSpeed;

	enum
	{
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
	int cloudMaxAlpha;//opacity of clouds over open ground; the fog of war is overcast regardless
	int cloudMaxSpeed;//in tenths of a pixel per frame
	int cloudWindStability;//how much will the wind change
	int cloudStability;//how much will the clouds change shape; higher holds them steadier
	int cloudSize;//average length of a cloud in pixels; smaller gives the deck more relief per fogged patch
	int cloudHeight;//(cloud - ground) / (eyes - ground)
	int cloudCoverage;//relief of the deck in percent; the higher the more of it catches the sun
	int cloudShadeAlpha;//cloud strength over ground already explored but out of sight, 0 disables

	int tempUnit;
	int tempUnitFuture;

	void resetDefaultUnitsAssigned();
	void resetCloudSettings();
	void resetDefaultFlagRadius();
};

//Version 1 - Resets default units assigned and keyboard shortcuts
//Version 2 - Resets the cloud settings, whose scale and opacity changed meaning
#define SETTINGS_VERSION 2

