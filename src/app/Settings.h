// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "Header.h"
#include "ExperimentalFeatures.h"
#include <RendererSelection.h>
#include <string>
#include <cstdint>
#include <map>
#include <optional>
#include "field/GradientSelection.h"
#include "IntBuildingType.h"
class BuildingType;
class BuildingsTypes;
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
	 * Names exceeding MAX_NAME_LENGTH UTF-8 bytes leave the current name intact.
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
    // Local drawing ceiling; zero is Unlimited. Never part of saves or orders.
    int targetRenderFps = 60;
    static constexpr int RENDER_FPS_PRESETS[] = {25, 30, 60, 90, 120, 144, 165, 240, 0};
	Uint32 screenFlags;
    GAGCore::RendererBackend rendererBackend = GAGCore::RendererBackend::Auto;
	Uint32 optionFlags;
	bool automaticTorus; // Opt-in movement-triggered overview; local presentation only.
	std::string language;
	Uint32 musicVolume;
	std::string musicSet; // Empty selects a random soundtrack at match start.
	Uint32 voiceVolume;
	int mute;
	bool introductionOffered = false; ///< Local first-run recommendation; old profiles migrate as offered.
	bool audioPreferenceChosen = false; ///< Explicit first-run audio choice; old volume defaults are not consent.
	/// Age band for social features (src/app/AgeGate.h): "unknown", "under_13" or
	/// "13_plus", and who set it ("self", "apple", "google_play", "account" or "").
	/// No birthdate is stored.
	std::string ageBand = "unknown";
	std::string ageBandSource;
	// Call before constructing the mixer, after the host presentation is known.
	void applyAudioStartupPolicy(bool mobile) { if (mobile && !audioPreferenceChosen) mute = 1; }
	int version;
	bool rememberUnit;
	bool scrollWheelEnabled;
	// Local camera controls, independent of saves and simulation.
	bool edgeScrollFullscreen = true;
	bool edgeScrollWindowed = false;
	bool edgeScrollingEnabled(bool fullscreen) const
	{
		return fullscreen ? edgeScrollFullscreen : edgeScrollWindowed;
	}
	bool hiveMindEnabled;
	bool hiveMindSupervision;
	// Device-adaptive preferences: Auto follows DeviceProfile; Off and On are
	// the player's explicit choice and are never overridden.
	enum class AdaptiveChoice : std::uint8_t { Auto, Off, On };
	AdaptiveChoice highResolutionArtworkChoice = AdaptiveChoice::Auto;
	// The effective value every renderer reads: the explicit choice, or Auto
	// resolved against the device profile. A session step-down lowers it on
	// Auto for the rest of the run without saving anything.
	bool highResolutionArtwork = true;
	bool highResolutionArtworkSteppedDown = false;
	void setHighResolutionArtworkChoice(AdaptiveChoice choice);
	// Resolve Auto values against DeviceProfile::current() (load() calls it).
	void resolveAdaptiveDefaults();
	static const char *adaptiveChoiceName(AdaptiveChoice choice);
	static std::optional<AdaptiveChoice> parseAdaptiveChoice(const std::string &text);
	// Local rendering preferences; never serialized into games or orders.
	bool showColonySkins = true;
	bool clouds;
	bool cloudShadows;
	bool buildingParticles;
	bool fullMagicEffects;
	bool translucentPanels;
	bool translucentPathLines;
	bool smoothProgressIndicators;
	bool decorativeAnimations;
	//! Draw units moving and animating between simulation ticks.
	//! Off draws exactly the simulated positions. Not part of setGraphicsDetail.
	bool unitInterpolation = true;
	//! Adapt bars, zones, units and terrain to the zoom instead of scaling them with the map.
	bool adaptiveZoomDetail = true;
	//! Fade tiles into and out of the fog of war instead of switching them.
	bool smoothFog = true;
	void setGraphicsDetail(bool full);
	static constexpr Uint32 LEGACY_LOW_DETAIL = 0x1;
	//! The eight effects setGraphicsDetail sets together follow the device
	//! tier (on except on a Low device) until the player changes one of them.
	//! On by default in the browser, whose build used to start with them all
	//! off; native builds start with full detail and leave this off.
	bool graphicsDetailAdaptive;
	static bool isGraphicsDetailEffect(bool Settings::*field);
	//! A player's choice of one effect; a detail effect ends the adaptive default.
	void chooseEffect(bool Settings::*field, bool on);
	/// Periodically saves the game in progress as "Auto save".
	bool autosaveGames;
	/// Settings > Privacy: send crash reports without asking (src/app/CrashCapture.h).
	bool alwaysSendCrashReports;
	// Local compute preference. An absent value defaults off in both graphical and headless sessions.
	std::optional<bool> gpuCompute;
	std::optional<gradient_kernel::Backend> gradientBackend;
    gradient_kernel::Algorithm gradientAlgorithm=gradient_kernel::Algorithm::Frozen8;
    gradient_kernel::Configuration gradientConfiguration() const {
        const auto b=gradientBackend.value_or(gpuCompute.value_or(false)?gradient_kernel::Backend::OpenCL:gradient_kernel::Backend::CPU);
        return {b,gradient_kernel::isGPU(b)?gradientAlgorithm:gradient_kernel::Algorithm::Buckets};
    }
    bool gpuComputeEnabled() const { return gradient_kernel::isGPU(gradientConfiguration().backend); }
	/// Experimental features to bake into every new game this player starts or
	/// hosts (Settings > Experiments). Saved as comma-separated keys; a key this
	/// build no longer knows is dropped on load.
	ExperimentSet experiments;
	/// Simulation speed preset. Zero is the normal 30 ticks/second;
	/// higher values progressively reduce delays and then skip rendered frames.
	int gameSpeed;
    std::string interfacePresentation = "automatic";
    // Local desktop automation preferences; never part of simulation or saved games.
    bool mcpEnabled = false;
    int mcpPort = 27202;
    // glob2 play --mcp-port: enables the listener on that port for this run only.
    // Never saved; editing the MCP settings replaces it with the saved choice.
    int mcpLaunchPort = 0;
    bool mcpActive() const { return mcpEnabled || mcpLaunchPort; }
    int mcpActivePort() const { return mcpLaunchPort ? mcpLaunchPort : mcpPort; }
    // Turn a launch override into the saved choice, before the player edits it.
    void adoptMcpLaunchOverride()
    {
        if (!mcpLaunchPort) return;
        mcpEnabled = true;
        mcpPort = mcpLaunchPort;
        mcpLaunchPort = 0;
    }
    // Interface theme ids (data/themes) for menus and for matches and the editor.
    std::string menuTheme = "light";
    std::string gameTheme = "dark";
    // Size of all touch interface text, 100..150 percent: menus, dialogs and the
    // HUD. Local UI preference; never part of saves/orders. Replaces the former
    // mobileDialogTextPercent, which only compensated in-game dialogs and is ignored.
    int textSizePercent = 100;
    // The statistic last looked at after a match (a metric catalog id; empty is the
    // overview) and the rate averaging window in samples (Stats::DEFAULT_RATE_WINDOW,
    // about two minutes). Local UI preference.
    std::string statsMetric;
    int statsWindow = 6;
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
	bool thumbSideChosen = false; ///< First-launch hand selection has been saved.
	/// Resolves the platform default: Google Maps (Android) zooms in on a
	/// downward drag, Apple Maps and others on an upward drag.
	bool dragUpZoomsIn(void) const;

	enum
	{
		GAME_SPEED_MINIMUM = -3,
		GAME_SPEED_NORMAL = 0,
		GAME_SPEED_MAXIMUM = 10,
	};

	/// Rounded milliseconds for presentation; simulation pacing uses nanoseconds.
	int getGameSpeedStepDuration(void) const;
	/// Fractional tick interval, accumulated before rounding host waits.
	std::uint64_t getGameSpeedStepDurationNs() const;
	/// Number of simulation steps between rendered frames for the selected preset.
	int getGameSpeedRenderInterval(void) const;
	/// Human-readable multiplier used by settings, in-game options and notifications.
	std::string getGameSpeedText(void) const;
	/// Change presets while keeping the value within the supported range.
	void changeGameSpeed(int amount);

	

	// Preferences are isolated by complete catalog fingerprint and stable variant
	// key: a custom catalog may reuse stock keys without changing stock defaults.
	std::map<std::string, int> buildingAssignments;
	std::map<std::string, int> buildingRadii;
	int buildingAssignment(const std::string& catalog, const BuildingType& type) const;
	int buildingRadius(const std::string& catalog, const BuildingType& type) const;
	void setBuildingAssignment(const std::string& catalog, const BuildingType& type, int value);
	void setBuildingRadius(const std::string& catalog, const BuildingType& type, int value);

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

// Window flags for the startup graphic context. Desktop and mobile builds let
// Auto follow the measured per-platform renderer order (PREFERDEVICE; desktop
// also keeps USEGPU for Legacy GL); browsers keep their probed surface. The
// request is not persisted, and an explicit renderer choice still overrides it.
unsigned startupScreenFlags(unsigned savedFlags);

// Version 2 imports the historical family/level preferences into catalog keys.
#define SETTINGS_VERSION 2
