// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2007 Stephane Magnenat & Luc-Olivier de Charrière
#include "GlobalContainer.h"
#include "CommandLine.h"
#include "ComputeThreads.h"
#include "ApplicationResourcePlan.h"
#include "ApplicationResourceObservations.h"
#include "AndroidResourceObservations.h"
#include "ApplicationPhaseResources.h"
#include "ResourcePlanJson.h"
#include <nlohmann/json.hpp>
#include <limits>
#include "field/GradientBackend.h"
#include <cstdlib>
#include <cstring>
#include "AINames.h"
#include "FileManager.h"
#include "InviteLink.h"
#include <Toolkit.h>
#include <GraphicContext.h>
#include <GameplayRecording.h>
#include <algorithm>
#include <sstream>
#include <stdexcept>

void GlobalContainer::applyCommand(const Cli::Request &request)
{
	const auto &r = request;
	for (const auto &path : r.all("--data-dir"))
		fileManager->addDir(path);
	if (r.has("--compute-threads"))
		computeThreads = parseComputeThreadCount(r.get("--compute-threads"));
	if (r.has("--gpu"))
		gpuComputeOverride = r.get("--gpu") == "on";
    if (r.has("--execution-profile"))
        executionResourceRequest.explicitProfile = resource_policy::parseExecutionProfile(r.get("--execution-profile"));
    if (r.has("--memory-budget-bytes"))
        executionResourceRequest.planningMemoryBytes = resource_policy::parsePlanningMemoryBytes(r.get("--memory-budget-bytes"));
	auto toggle = [&](const char *name, unsigned bit)
	{
		if (r.has(std::string("--") + name))
			settings.screenFlags |= bit;
		if (r.has(std::string("--no-") + name))
			settings.screenFlags &= ~bit;
	};
	toggle("fullscreen", GraphicContext::FULLSCREEN);
	toggle("resizable", GraphicContext::RESIZABLE);
	toggle("custom-cursor", GraphicContext::CUSTOMCURSOR);
	if (r.has("--renderer"))
	{
        rendererLaunchOverride = GAGCore::parseRendererBackend(r.get("--renderer"));
	}
	if (r.has("--graphics-detail"))
		settings.setGraphicsDetail(r.get("--graphics-detail") == "full");
	if (r.has("--mute"))
		settings.mute = 1;
	if (r.has("--no-mute"))
		settings.mute = 0;
	if (r.has("--username"))
		settings.setUsername(r.get("--username").c_str());
	if (r.has("--editor-script"))
	{
		if (r.get("--editor-script") == "usl")
			settings.optionFlags |= OPTION_MAP_EDIT_USE_USL;
		else
			settings.optionFlags &= ~OPTION_MAP_EDIT_USE_USL;
	}
	if (r.has("--window-size"))
	{
		const auto text = r.get("--window-size");
		const auto x = text.find('x');
		settings.screenWidth = std::max(640, std::stoi(text.substr(0, x)));
		settings.screenHeight = std::max(480, std::stoi(text.substr(x + 1)));
	}
	if (r.has("--record"))
		recordingPath = r.get("--record");
	if (r.has("--videoshot"))
		videoshotName = r.get("--videoshot");
	if (r.has("--record") || r.has("--videoshot"))
	{
		auto &options = GAGCore::Recording::recorder().options;
		options.fps = std::stoi(r.get("--record-fps"));
		options.crf = std::stoi(r.get("--record-crf"));
		options.chapterTicks = unsigned(std::stoul(r.get("--record-chapter-ticks")));
		options.encoder = r.get("--record-encoder") == "software"
							  ? GAGCore::Recording::EncoderPreference::Software
							  : GAGCore::Recording::EncoderPreference::Auto;
	}
	if (r.command == "replay")
	{
		replaying = true;
		replayFileName = r.positionals.at(0);
	}
	if (r.command == "game repeat")
	{
		runNoX = !r.has("--display");
		automaticEndingGame = true;
		runNoXGameName = r.positionals.at(0);
		automaticEndingSteps = std::stoi(r.get("--ticks"));
		runNoXCountRuns = std::stoi(r.get("--runs"));
	}
	if (r.command == "dev random-games")
	{
		runTestGames = true;
		runNoX = !r.has("--display");
		automaticEndingGame = true;
		automaticGameGlobalEndConditions = true;
		if (r.has("--ticks"))
			automaticEndingSteps = std::stoi(r.get("--ticks"));
		runTestGamesCount = std::stoi(r.get("--runs"));
		testGamesMap = r.get("--map");
		testGamesSaveGameAs = r.get("--save-game-as");
		auto aiList = [&](const std::string &key, std::vector<int> &out)
		{
			std::istringstream list(r.get(key));
			std::string item;
			while (std::getline(list, item, ','))
			{
				const auto id = AINames::parseAIName(item);
				if (id <= 0 || id == AI::JAVASCRIPT)
					throw std::invalid_argument(key + ": invalid AI '" + item +
												"'; valid: " + AINames::validAINames());
				out.push_back(id);
			}
		};
		aiList("--ai-types", testGamesAIPool);
		aiList("--matchup", testGamesMatchup);
	}
	if (r.command == "dev stress-maps")
	{
		runTestMapGeneration = true;
		runNoX = true;
	}
	if (r.command == "dev textshots")
		GAGCore::DrawableSurface::translationPicturesDirectory = r.get("--output-dir");
	if (r.command.rfind("dev dump-", 0) == 0)
		runNoX = true;
	Online::acceptLaunchRequest(r);
}

bool GlobalContainer::gpuComputeEnabled() const
{
	if (gpuComputeOverride.has_value()) return *gpuComputeOverride;
	// Retain binary command-line compatibility for reproducible benchmarks.
	if (const auto* backend = std::getenv("GLOB2_GRADIENT_BACKEND"); backend && *backend)
		return std::strcmp(backend, "opencl") == 0;
	return settings.gpuComputeEnabled(runNoX);
}

void GlobalContainer::configureGradientBackend() const
{
	gradient_kernel::setBackend((frozenResourcePlan ? resolvedGpuComputeEnabled() : gpuComputeEnabled()) ? gradient_kernel::Backend::OpenCL
													 : gradient_kernel::Backend::CPU);
}

void GlobalContainer::refreshResourcePlan(bool ownerOnly)
{
    resource_policy::ApplicationResourceInputs input;
    input.request = executionResourceRequest;
    input.request.constraints=resource_policy::discoverApplicationRuntimeConstraints(input.request.constraints);
    input.savedProfile = settings.executionProfile;
    input.headless = runNoX;
    input.ownerOnly = ownerOnly;
    input.legacyRequestedComputeParticipants = computeThreads;
    input.legacyGpuComputeEnabled = gpuComputeEnabled(); // Preserve legacy CLI/environment/saved/context ordering.
    const auto *environment = std::getenv("GLOB2_GRADIENT_BACKEND");
    input.legacyGpuComputeExplicit = gpuComputeOverride.has_value() || (environment && *environment);
    if (settings.targetRenderFps < 0) throw std::invalid_argument("Render frame rate must be nonnegative");
    input.currentPresentation.frameRate = unsigned(settings.targetRenderFps);
    // Independent legacy appearance flags and discretionary work retain their
    // current consumers. No numeric detail/preparation tier is inferred/applied.
    CpuAvailabilityObservation sampledCpuAvailability;
    input.capabilities = resource_policy::discoverApplicationResourceCapabilities(&sampledCpuAvailability);
    frozenResourcePlan = resource_policy::resolveApplicationResourcePlan(input);
    frozenCpuAvailability=sampledCpuAvailability;
    frozenResourceObservationNs=SDL_GetTicksNS();
}
const resource_policy::ResourcePlan &GlobalContainer::resourcePlan() const
{
    if (!frozenResourcePlan) throw std::logic_error("Resource plan requires explicit session/setup refresh");
    return *frozenResourcePlan;
}
unsigned GlobalContainer::resolvedComputeParticipants() const
{
    return resourcePlan().resources.computeParticipants;
}
bool GlobalContainer::resolvedGpuComputeEnabled() const
{
    return resourcePlan().resources.computeGpu != resource_policy::GpuPolicy::Disabled;
}
nlohmann::json GlobalContainer::resourcePlanJson() const
{
    auto value = resource_policy::describeResourcePlan(resourcePlan());
    value["frozen_host_observation_sdl_ns"]=frozenResourceObservationNs;
    value["cpu_availability_observation"] = {
        {"source",cpuAvailabilitySourceName(frozenCpuAvailability.source)},
        {"hardware_hint",frozenCpuAvailability.hardwareHint ? nlohmann::json(frozenCpuAvailability.hardwareHint) : nlohmann::json(nullptr)},
        {"eligible_logical_cpu_count",frozenCpuAvailability.eligibleLogicalCpuCount ? nlohmann::json(*frozenCpuAvailability.eligibleLogicalCpuCount) : nlohmann::json(nullptr)},
        {"scope","creating-thread affinity/cpuset where observed; not CPU-time quota, physical cores or reserved CPU capacity"}
    };
    const auto current=resource_policy::discoverApplicationRuntimeConstraints({});
    value["current_host_observations"]=resource_policy::describeRuntimeConstraints(current);
    value["current_host_observations"]["sampled_at_sdl_ns"]=SDL_GetTicksNS();
    value["current_host_observations"]["clock_scope"]="SDL monotonic ns; separate from performance steady clock origin";
    value["current_host_observations"]["scope"]="end/setup snapshot, not frozen session constraints or policy adaptation";
    const auto androidObservation=resource_policy::discoverAndroidResourceObservations();
    value["current_android_observations"] = {
        {"schema_version",1}, {"source","Android PowerManager; unknown on other platforms or unavailable JNI/service/API"},
        {"battery_saver",androidObservation.batterySaver ? nlohmann::json(*androidObservation.batterySaver) : nlohmann::json(nullptr)},
        {"raw_thermal_status",androidObservation.rawThermalStatus ? nlohmann::json(*androidObservation.rawThermalStatus) : nlohmann::json(nullptr)},
        {"recognized_thermal_status",androidObservation.recognizedThermalStatus ? nlohmann::json(*androidObservation.recognizedThermalStatus) : nlohmann::json(nullptr)},
        {"sampled_at_sdl_ns",SDL_GetTicksNS()}, {"clock_scope","SDL monotonic ns; sampled after per-call platform query"},
        {"scope","setup/end observation; raw OS thermal state, not temperature, sensor coverage, CPU capacity or profile tuning"}
    };
    value["application_adapter"] = {
        {"frozen_session_plan", true},
        {"constraint_precedence", "caller provided values win; missing values use known host observations or remain unknown"},
        {"host_constraint_sources", "SDL power state; native lifecycle events or browser DOM visibility; SDL low-memory event sticky, no cleared signal"},
        {"battery_saver_source", "caller supplied wins; otherwise Android PowerManager when available; unknown elsewhere, independent of battery use"},
        {"constraint_sampling", "explicit setup/end snapshot; no per-frame power polling or live compute resizing"},
        {"independent_phase_compute_override_scope", "simulation CPU/GPU overrides excluded from independent owner/UI phases; phase-specific overrides remain explicit"},
        {"cpu_discovery", "known allocation only from observed eligible mask; hardware hint kept separate, unknown automatic uses1; not physical cores or CPU-time quota"},
        {"required_live_bytes_scope", "unestimated lower bound; not a complete heap/live ownership census"},
        {"memory_budget_actions_applied", false}, {"render_policy_actions_applied", false},
        {"render_detail_scope", "legacy appearance flags preserved; no numeric tier applied"},
        {"presentation_preparation_scope", "planning metadata only; existing required and discretionary consumers unchanged"}
    };
    if(startupAssetResources) {
        const auto &assets=*startupAssetResources;
        value["startup_asset_phase"]=resource_policy::describeResourcePlan(assets.plan);
        auto &phase=value["startup_asset_phase"];
        phase["planned_cpu_workers"]=assets.options.cpuThreads; phase["planned_io_workers"]=assets.options.ioThreads;
        phase["admission_bytes"]=assets.options.memoryBytes;
        phase["admission_scope"]="active/encoded credits with oversized-job progress, not recoverable cache or RSS";
        phase["original_cpu_environment"]=assets.originalCpuEnvironment ? nlohmann::json(*assets.originalCpuEnvironment) : nlohmann::json(nullptr);
        phase["original_io_environment"]=assets.originalIoEnvironment ? nlohmann::json(*assets.originalIoEnvironment) : nlohmann::json(nullptr);
        phase["environment_normalization"]="existing validated parser; serial/browser may normalize requested roles to cooperative zero";
        if(const auto actual=GAGCore::Toolkit::assetMetrics()) {
            phase["actual_cpu_workers"]=actual->cpuThreads; phase["actual_io_workers"]=actual->ioThreads;
        } else { phase["actual_cpu_workers"]=nullptr; phase["actual_io_workers"]=nullptr; }
        phase["allocation_scope"]="phase-only budget, not a wholeprocess simultaneous admission ceiling";
    }
    return value;
}

resource_policy::PhaseResourceInputs GlobalContainer::phaseResourceInputs() const
{
    resource_policy::PhaseResourceInputs input;
    input.intent=executionResourceRequest;
    input.intent.constraints=resource_policy::discoverApplicationRuntimeConstraints(input.intent.constraints);
    if(computeThreads) input.intent.computeParticipants=computeThreads; // Record input before local phase exclusion.
    input.intent.savedProfile=settings.executionProfile;
    input.intent.contextDefault=resource_policy::ExecutionProfile::Auto;
    if(settings.targetRenderFps<0) throw std::invalid_argument("Render frame rate must be nonnegative");
    input.currentPresentation.frameRate=unsigned(settings.targetRenderFps);
    input.capabilities=resource_policy::discoverApplicationResourceCapabilities();
#ifdef __EMSCRIPTEN__
    input.assetIoWorkerLimit=0; // Browser MEMFS reads stay on the owner.
#endif
    return input;
}
resource_policy::ResourcePlan GlobalContainer::ownerPhaseResourcePlan() const
{
    return resource_policy::resolveOwnerPhase(phaseResourceInputs());
}
void GlobalContainer::prepareStartupResources()
{
    if(startupAssetResources) return;
    const auto input=phaseResourceInputs();
    const auto defaults=GAGCore::AssetLoader::Options::environment(input.capabilities.allocatedCpuParticipants.value_or(1));
    // environment() already validates syntax and bounds; keep pre-clamp intent.
    const char *overrideText=std::getenv("GLOB2_ASSET_THREADS");
    const auto explicitWorkers=overrideText && *overrideText ? std::optional<unsigned>(static_cast<unsigned>(std::strtoul(overrideText,nullptr,10))) : std::nullopt;
    const char *ioOverrideText=std::getenv("GLOB2_ASSET_IO_THREADS");
    const auto explicitIo=ioOverrideText && *ioOverrideText ? std::optional<unsigned>(static_cast<unsigned>(std::strtoul(ioOverrideText,nullptr,10))) : std::nullopt;
    auto candidate=resource_policy::resolveAssetPhase(input,defaults,explicitWorkers,explicitIo);
    if(overrideText && *overrideText) candidate.originalCpuEnvironment=overrideText;
    if(ioOverrideText && *ioOverrideText) candidate.originalIoEnvironment=ioOverrideText;
    if(!GAGCore::Toolkit::configureAssets(candidate.options))
        throw std::logic_error("Startup resource plan must precede the first asset request");
    startupAssetResources=candidate;
}
void GlobalContainer::refreshPresentationResources()
{
    presentationResources=ownerPhaseResourcePlan();
    if(gfx) gfx->setTargetRenderFps(presentationResources->resources.render.frameRate);
}
unsigned GlobalContainer::resolvedRenderFrameRate() const
{
    return presentationResources ? presentationResources->resources.render.frameRate : unsigned(settings.targetRenderFps);
}
