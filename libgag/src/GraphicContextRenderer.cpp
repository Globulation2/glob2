// SPDX-License-Identifier: GPL-3.0-or-later
#include "GraphicContextPrivate.h"
#include <cstring>
#include <SoftwareFramePresenter.h>
#include <algorithm>
#include <RenderDevice.h>
#include <ProgrammableGLDevice.h>
#include <WebGPUDevice.h>
#include <SDLGPUDevice.h>
#include <FileManager.h>
#include <Toolkit.h>
#include <filesystem>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace GAGCore
{
#ifdef __EMSCRIPTEN__
extern "C" {
void glob2_surface_retire();
int glob2_surface_prepare(int gpu, int width, int height);
int glob2_surface_poll();
void glob2_surface_finish(int success);
void glob2_surface_error(char *destination, int size);
int glob2_surface_webgpu_available();
}
#endif
SDL_Surface *GraphicContext::getSDLSurface()
{
    // Accelerated backends keep a CPU facade for dimensions/availability. A
    // getter must not submit their frames or imply a GPU readback. Temporary
    // CPU passes also expose their borrowed target only after drawing finishes.
    if (renderer && (renderer == softwareRasterizer.get() ||
                     (offscreenPass && renderer != deviceRenderer.get())))
        renderer->flush();
    return sdlsurface;
}

std::optional<RendererBackend> GraphicContext::initialBackend;
std::string GraphicContext::rendererShaderDirectory()
{
    if (const char *directory = SDL_getenv_unsafe("GLOB2_RENDERER_SHADER_DIR")) return directory;
    if (Toolkit::getFileManager())
        for (unsigned i = 0; i < Toolkit::getFileManager()->getDirCount(); ++i) {
            auto directory = Toolkit::getFileManager()->getDir(i) + "/shaders";
            if (std::filesystem::exists(directory + "/webgl2/primitive.frag")) return directory;
        }
#if defined(GLOB2_RENDER_SHADER_DIR) && !defined(GLOB2_INSTALLED_PATHS)
    return GLOB2_RENDER_SHADER_DIR;
#else
    return "data/renderer-shaders";
#endif
}

bool GraphicContext::flushDeviceFrame()
{
    if (!renderDevice()) return false;
    renderer->flush();
    return didRecordDeviceFrame(*renderer);
}

bool GraphicContext::deferDeviceFrame()
{
    return renderDevice() && GAGCore::deferDeviceFrame(*renderer);
}

bool GraphicContext::setDeviceTargetScissor(const SDL_Rect *pixels)
{
    return renderDevice() && GAGCore::setDeviceTargetScissor(*renderer, pixels);
}

bool GraphicContext::beginDeviceTarget(TextureSnapshot target, float logicalWidth, float logicalHeight)
{
    if (!presentationDevice || !renderer || !target) return false;
    if (!pushDeviceTarget(*renderer, target, logicalWidth, logicalHeight)) return false;
    deviceTargetStates.push_back({clipRect, renderTargetScale, offscreenPass, mapTransformActive, uiTransformActive});
    mapTransformActive = uiTransformActive = false;
    offscreenPass = true;
    renderTargetScale = std::min(target->descriptor.width / logicalWidth, target->descriptor.height / logicalHeight);
    clipRect = {0, 0, int(std::ceil(logicalWidth)), int(std::ceil(logicalHeight))};
    renderer->clip(&clipRect);
    return true;
}
void GraphicContext::endDeviceTarget()
{
    if (deviceTargetStates.empty()) throw std::logic_error("No active device target.");
    popDeviceTarget(*renderer);
    const auto previous = deviceTargetStates.back(); deviceTargetStates.pop_back();
    clipRect = previous.clip; renderTargetScale = previous.scale; offscreenPass = previous.offscreen;
    mapTransformActive = previous.map; uiTransformActive = previous.ui;
    renderer->clip(&clipRect);
}
bool GraphicContext::submitDevicePacket(DrawPacket packet)
{
    return presentationDevice && renderer && recordDevicePacket(*renderer, std::move(packet)) &&
        didRecordDeviceFrame(*renderer);
}

bool GraphicContext::beginRenderFrame()
{
    try {
    pollCaptures();
    if (awaitingBrowserSurface || awaitingDevicePreparation || (transitionPixels && nextBackend)) applyQueuedBackend();
    return !awaitingBrowserSurface && !awaitingDevicePreparation && !awaitingSwitchCapture && window && sdlsurface && (!presentationDevice || presentationDevice->canBeginFrame()) && renderPacer.begin(SDL_GetTicksNS());
    } catch (const std::exception &error) { handleDeviceFailure(error.what()); return false; }
}

RendererCapabilities GraphicContext::rendererCapabilities() const
{
    if (offscreenPass && renderer != deviceRenderer.get())
        return {};
    if (changingBackend) return retiringCapabilities;
    const bool gl = context && (optionFlags & USEGPU);
    return {.accelerated = gl || bool(portableRenderer) || bool(presentationDevice), .torus = gl || (presentationDevice && presentationDevice->capabilities().torus),
        .liveSkins = gl || (presentationDevice && presentationDevice->capabilities().skinMaterials), .textureFiltering = gl || bool(portableRenderer) || bool(presentationDevice),
        .asynchronousCapture = presentationDevice && presentationDevice->capabilities().asynchronousCapture,
        .alphaMapCellCentres = gl || bool(presentationDevice), .cloudLayers = gl || bool(presentationDevice),
        .textureMipmaps = gl || bool(presentationDevice)};
}

std::vector<RendererAvailability> GraphicContext::rendererAvailability() const
{
    std::vector<RendererAvailability> choices;
    for (auto backend : RendererBackends) {
        bool available = backend == RendererBackend::Auto || backend == RendererBackend::Software ||
            backend == RendererBackend::PortableSDL;
        std::string reason;
#ifdef HAVE_OPENGL
        if (backend == RendererBackend::LegacyGL) available = true;
#endif
        if (backend == RendererBackend::ProgrammableGL) available = programmableGLCompiled();
#ifdef __EMSCRIPTEN__
        if (backend == RendererBackend::WebGPU) {
            available = std::filesystem::exists(rendererShaderDirectory() + "/webgpu/primitive.frag.wgsl");
            if (!available) reason = "This build does not contain the generated WebGPU shaders.";
            else if (!glob2_surface_webgpu_available()) {
                available = false;
                reason = "WebGPU is unavailable in this browser's presentation thread.";
            }
        }
#endif
#ifndef __EMSCRIPTEN__
        if (backend == RendererBackend::SDLGPU) {
            reason = sdlGPUUnavailableReason(rendererShaderDirectory());
            available = reason.empty();
        }
#endif
        if (!available && reason.empty()) reason = "This backend is not implemented in this build.";
        choices.push_back({backend, available, std::move(reason)});
    }
    return choices;
}

bool GraphicContext::requestBackendSwitch(RendererBackend backend)
{
    const auto choices = rendererAvailability();
    const auto found = std::find_if(choices.begin(), choices.end(), [backend](const auto &choice) {
        return choice.backend == backend;
    });
    if (found == choices.end() || !found->available) {
        if (changingBackend) return false;
        nextBackend.reset();
        backendStatus.requested = backend;
        backendStatus.phase = RendererSwitchPhase::Failed;
        backendStatus.reason = found == choices.end() ? "Unknown rendering backend." : found->reason;
        ++backendStatus.serial;
        return false;
    }
    nextBackend = backend;
    nextBackendPersists = true;
    if (changingBackend) return true;
    backendStatus.requested = backend;
    backendStatus.phase = RendererSwitchPhase::Queued;
    backendStatus.reason.clear();
    ++backendStatus.serial;
    return true;
}

bool GraphicContext::activateBackend(RendererBackend backend, SDL_Surface *previousFrame)
{
    Uint32 flags = optionFlags & ~(USEGPU | PORTABLEGPU);
    if (backend == RendererBackend::LegacyGL) flags |= USEGPU;
    if (backend == RendererBackend::PortableSDL) flags |= PORTABLEGPU;
    int x = switchWindowX, y = switchWindowY;
    if (window) SDL_GetWindowPosition(window, &x, &y);
    const int width = requestedW, height = requestedH;
    const bool responsive = responsiveViewport;
    const int minimumW = responsiveMinW, minimumH = responsiveMinH;
    configuredBackend = backend;
    if (!setRes(width, height, flags)) throw std::runtime_error(SDL_GetError());
    if (!(flags & FULLSCREEN)) SDL_SetWindowPosition(window, x, y);
    if (responsive && !setResponsiveViewport(true, minimumW, minimumH))
        throw std::runtime_error("Cannot restore responsive viewport.");
    if (previousFrame) {
        DrawableSurface retained(previousFrame);
        setClipRect();
        drawSurface(0, 0, getW(), getH(), &retained);
        // First presentation belongs to the transaction. Do not recursively
        // capture recording frames or consume another backend request.
        if (renderer && !nativeSoftware) {
            renderer->present();
            if (presentationDevice && !didPresentDeviceFrame(*renderer)) throw std::runtime_error("Retained frame exceeded device admission limits.");
        }
        else {
            cacheFrame();
            if (optionFlags & USEGPU) swapBuffers();
            else presentLastFrame();
        }
    }
    return true;
}

void GraphicContext::applyQueuedBackend()
{
    if (offscreenPass || mapTransformActive || uiTransformActive || awaitingSwitchCapture) return;
#ifdef __EMSCRIPTEN__
    if (awaitingBrowserSurface && glob2_surface_poll() == 0) return;
#endif
    if (!awaitingBrowserSurface && !awaitingDevicePreparation) {
        if (!nextBackend || changingBackend) return;
        const auto resolved = *nextBackend == RendererBackend::Auto ? automaticBackend : *nextBackend;
        if (resolved != backendStatus.active && !transitionPixels && !recoveringBackend) return;
        if (!recoveringBackend) backendStatus.requested = *nextBackend;
        // A queued successor can start and finish between two UI polls. Its
        // requested preference must remain observable even if the phase returns
        // to Complete without the UI seeing the intermediate transition.
        ++backendStatus.serial;
        retiringCapabilities = rendererCapabilities();
        switchStarted = SDL_GetTicksNS();
        switchPersists = nextBackendPersists;
        nextBackendPersists = true;
        switchingFrom = backendStatus.active;
        switchingTo = *nextBackend == RendererBackend::Auto ? automaticBackend : *nextBackend;
        nextBackend.reset();
    }
    const RendererBackend requested = backendStatus.requested;
    const RendererBackend previous = switchingFrom;
    const RendererBackend destination = switchingTo;
    if (destination == previous) {
        backendStatus.phase = RendererSwitchPhase::Complete;
        ++backendStatus.serial;
        transitionPixels.reset();
        if (switchPersists && backendPreferenceCallback) backendPreferenceCallback(requested);
        return;
    }
    backendStatus.phase = RendererSwitchPhase::Preparing;
    if (!transitionPixels && !recoveringBackend) {
        backendStatus.phase = RendererSwitchPhase::Failed;
        backendStatus.reason = "Cannot retain the completed frame for renderer switching.";
        return;
    }
    changingBackend = true;
#ifdef __EMSCRIPTEN__
    if (destination == RendererBackend::WebGPU && !awaitingBrowserSurface) {
        const int ready = awaitingDevicePreparation ? glob2_webgpu_poll() : glob2_webgpu_prepare((rendererShaderDirectory() + "/webgpu").c_str());
        if (ready == 0) { awaitingDevicePreparation = true; return; }
        awaitingDevicePreparation = false;
        if (ready < 0) {
            char reason[512]{}; glob2_webgpu_error(reason, sizeof(reason));
            backendStatus.reason = reason; backendStatus.phase = RendererSwitchPhase::Failed;
            ++backendStatus.serial;
            changingBackend = false; transitionPixels.reset(); return;
        }
    }
    if (!awaitingBrowserSurface) {
        SDL_GetWindowPosition(window, &switchWindowX, &switchWindowY);
        int width = windowW, height = windowH;
        // Screen updates continue while the worker/page surface transaction is
        // pending. Keep their logical viewport alive independently of SDL's
        // retiring window; drawing admission remains suspended above.
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> logicalSurface(
            SDL_DuplicateSurface(sdlsurface), SDL_DestroySurface);
        if (!logicalSurface) {
            // No surface has been retired yet. A failed placeholder allocation
            // must leave the current device usable and the preference unchanged.
            backendStatus.reason = std::string("Cannot retain graphics viewport: ") + SDL_GetError();
            backendStatus.phase = RendererSwitchPhase::Failed;
            backendStatus.durationNanoseconds = SDL_GetTicksNS() - switchStarted;
            ++backendStatus.serial;
            changingBackend = false;
            transitionPixels.reset();
            return;
        }
        glob2_surface_retire();
        retirePresentationSurface();
        sdlsurface = logicalSurface.release();
        ownsSurface = true;
        awaitingBrowserSurface = true;
        if (glob2_surface_prepare(destination == RendererBackend::LegacyGL || destination == RendererBackend::ProgrammableGL || destination == RendererBackend::PortableSDL || destination == RendererBackend::WebGPU, width, height) == 0) return;
    }
#endif
    backendStatus.phase = RendererSwitchPhase::Presenting;
    try {
#ifdef __EMSCRIPTEN__
        if (glob2_surface_poll() < 0) {
            char reason[512]{}; glob2_surface_error(reason, sizeof(reason));
            throw std::runtime_error(reason);
        }
#endif
        activateBackend(destination, transitionPixels.get());
#ifdef __EMSCRIPTEN__
        glob2_surface_finish(1);
#endif
        backendStatus.phase = recoveringBackend ? RendererSwitchPhase::Failed : RendererSwitchPhase::Complete;
        if (!recoveringBackend) backendStatus.reason.clear();
    } catch (const std::exception &error) {
        backendStatus.reason = error.what();
#ifdef __EMSCRIPTEN__
        retirePresentationSurface();
        glob2_surface_finish(0);
#endif
        try { activateBackend(previous, transitionPixels.get()); }
        catch (const std::exception &restore) {
            backendStatus.reason += std::string("; restoring previous backend failed: ") + restore.what();
#ifdef __EMSCRIPTEN__
            // The restored canvas may already own an incompatible GL context.
            // Software recovery needs another surface transaction as well.
            glob2_surface_retire();
            retirePresentationSurface();
            sdlsurface = SDL_CreateSurface(
                std::max(1, int(windowW / uiScale + 0.5f)),
                std::max(1, int(windowH / uiScale + 0.5f)), SDL_PIXELFORMAT_ARGB8888);
            if (!sdlsurface)
                backendStatus.reason += std::string("; retaining software recovery viewport failed: ") + SDL_GetError();
            // The old surface is already gone. Keep recovery frame-polled even
            // if its temporary viewport allocation fails; null-surface guards
            // suspend layout/drawing until activateBackend retries Software.
            ownsSurface = sdlsurface != nullptr;
            switchingTo = RendererBackend::Software;
            recoveringBackend = true; switchPersists = false;
            awaitingBrowserSurface = true;
            glob2_surface_prepare(0, windowW, windowH);
            return;
#else
            try { activateBackend(RendererBackend::Software, transitionPixels.get()); }
            catch (const std::exception &fallback) { backendStatus.reason += std::string("; software fallback failed: ") + fallback.what(); }
#endif
        }
        backendStatus.phase = RendererSwitchPhase::Failed;
    }
    backendStatus.durationNanoseconds = SDL_GetTicksNS() - switchStarted;
    ++backendStatus.serial;
    changingBackend = false; recoveringBackend = false;
    awaitingBrowserSurface = false;
    transitionPixels.reset();
    Sprite::refreshHighResolutionForRenderer();
    resetRenderPacing();
    if (backendStatus.phase == RendererSwitchPhase::Complete && switchPersists && backendPreferenceCallback)
        backendPreferenceCallback(requested);
}

void GraphicContext::pollCaptures()
{
    if (!presentationDevice) return;
    CaptureResult result;
    while (presentationDevice->pollCapture(result)) {
        auto found = recordingCaptures.find(result.request.identity);
        if (result.request.identity == std::numeric_limits<std::uint64_t>::max()) {
            awaitingSwitchCapture = false;
            if (result.cancelled || !result.pixels) {
                nextBackend.reset(); backendStatus.phase = RendererSwitchPhase::Failed;
                backendStatus.reason = "Cannot retain the completed frame for renderer switching.";
            } else transitionPixels = std::move(result.pixels);
            continue;
        }
        if (auto screenshot = screenshotCaptures.find(result.request.identity); screenshot != screenshotCaptures.end()) {
            if (!result.cancelled && result.pixels) saveScreenshot(*result.pixels, screenshot->second);
            else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Screenshot cancelled during graphics transition.");
            screenshotCaptures.erase(screenshot); continue;
        }
        if (auto memory = memoryCaptures.find(result.request.identity); memory != memoryCaptures.end()) {
            const auto frameIdentity = memory->second.frameIdentity;
            auto complete = std::move(memory->second.complete);
            memoryCaptures.erase(memory);
            complete(std::shared_ptr<SDL_Surface>(result.pixels.release(), SDL_DestroySurface),
                frameIdentity, result.cancelled ? "Capture cancelled during graphics transition" : "");
            continue;
        }
        if (found == recordingCaptures.end()) continue;
        if (result.cancelled || !result.pixels) Recording::recorder().dropCapture(found->second);
        else Recording::recorder().frame(*result.pixels, found->second);
        recordingCaptures.erase(found);
    }
}
void GraphicContext::cancelCaptures()
{
    try { pollCaptures(); }
    catch (const std::exception &error) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Retiring capture queue: %s", error.what()); }
    if (presentationDevice) {
        try { presentationDevice->cancelCaptures(); }
        catch (const std::exception &error) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cancelling capture queue: %s", error.what()); }
    }
    if (!screenshotCaptures.empty())
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cancelled %zu screenshot(s) during graphics transition.", screenshotCaptures.size());
    screenshotCaptures.clear(); awaitingSwitchCapture = false;
    auto pending = std::move(pendingMemoryCaptures);
    auto active = std::move(memoryCaptures);
    pendingMemoryCaptures.clear(); memoryCaptures.clear();
    for (auto &[id, request] : pending) request.complete({}, 0, "Capture cancelled during graphics transition");
    for (auto &[id, request] : active) request.complete({}, 0, "Capture cancelled during graphics transition");
    for (const auto &[identity, ticket] : recordingCaptures) Recording::recorder().dropCapture(ticket);
    recordingCaptures.clear();
}

void GraphicContext::captureBackendFrame()
{
    if (!nextBackend || changingBackend || offscreenPass || mapTransformActive || uiTransformActive) return;
    if (*nextBackend == backendStatus.active || (*nextBackend == RendererBackend::Auto && automaticBackend == backendStatus.active)) return;
    try {
        if (renderBatch) renderBatch->barrier();
        if (presentationDevice) { renderer->flush(); if (!didRecordDeviceFrame(*renderer)) return; }
        if (presentationDevice && backendStatus.active == RendererBackend::WebGPU) {
            renderer->flush();
            CaptureRequest request;
            request.identity = std::numeric_limits<std::uint64_t>::max();
            request.frameIdentity = submittedDeviceFrameIdentity(*renderer);
            presentationDevice->outputSize(request.width, request.height);
            if (!presentationDevice->capture(std::move(request))) return;
            awaitingSwitchCapture = true;
            return;
        }
        if (renderer) { renderer->flush(); transitionPixels.reset(renderer->capture()); }
#ifdef HAVE_OPENGL
        else if (context) {
            transitionPixels.reset(SDL_CreateSurface(drawableW, drawableH, SDL_PIXELFORMAT_RGBA32));
            if (!transitionPixels) throw std::runtime_error(SDL_GetError());
            GLint packAlignment;
            glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
#ifndef GLOB2_WEBGL2
            GLint rowLength;
            glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
            glPixelStorei(GL_PACK_ROW_LENGTH, 0);
#endif
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, drawableW, drawableH, GL_RGBA, GL_UNSIGNED_BYTE, transitionPixels->pixels);
            glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
#ifndef GLOB2_WEBGL2
            glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
#endif
            auto *bytes = static_cast<unsigned char *>(transitionPixels->pixels);
            for (int y = 0; y < transitionPixels->h / 2; ++y)
                for (int i = 0; i < transitionPixels->pitch; ++i)
                    std::swap(bytes[y * transitionPixels->pitch + i], bytes[(transitionPixels->h - 1 - y) * transitionPixels->pitch + i]);
        }
#endif
        else transitionPixels.reset(SDL_DuplicateSurface(sdlsurface));
        if (!transitionPixels) throw std::runtime_error(SDL_GetError());
    } catch (const std::exception &error) {
        backendStatus.reason = error.what();
        backendStatus.phase = RendererSwitchPhase::Failed;
        nextBackend.reset();
        return;
    }
}

bool GraphicContext::saveScreenshot(SDL_Surface &pixels, const std::string &name)
{
    for (unsigned i = 0; i < Toolkit::getFileManager()->getDirCount(); ++i) {
        const auto path = Toolkit::getFileManager()->getDir(i) + "/" + name;
        if (SDL_SaveBMP(&pixels, path.c_str())) return true;
    }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot save screenshot: %s", SDL_GetError());
    return false;
}
void GraphicContext::captureScreenshot()
{
    if (pendingScreenshot.empty() || nextBackend || changingBackend) return;
    try {
        if (presentationDevice) {
            renderer->flush();
            if (!didRecordDeviceFrame(*renderer)) return;
            CaptureRequest request; request.identity = ++screenshotIdentity;
            request.frameIdentity = submittedDeviceFrameIdentity(*renderer);
            presentationDevice->outputSize(request.width, request.height);
            if (!presentationDevice->capture(std::move(request))) return;
            screenshotCaptures.emplace(screenshotIdentity, std::move(pendingScreenshot));
        } else {
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
                renderer ? renderer->capture() : SDL_ConvertSurface(sdlsurface, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
            if (pixels) saveScreenshot(*pixels, pendingScreenshot);
        }
        pendingScreenshot.clear();
    } catch (const RenderAdmissionDeferred &) {}
    catch (const std::exception &error) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Screenshot: %s", error.what()); pendingScreenshot.clear(); }
}
void GraphicContext::recoverPresentationDevice()
{
    cancelCaptures();
    resetDeviceSkinState(); deviceTargetStates.clear();
    ++glContextGeneration;
    renderer = nullptr; deviceRenderer.reset(); presentationDevice.reset();
    // Recompile programs and recreate every graphics object after context loss.
    // Logical source pixels and immutable geometry remain owned by the registry.
    if (configuredBackend == RendererBackend::ProgrammableGL)
        presentationDevice = makeProgrammableGLDevice(window, rendererShaderDirectory());
#ifndef __EMSCRIPTEN__
    else presentationDevice = makeSDLGPUDevice(window, drawableW, drawableH, rendererShaderDirectory());
#else
    else presentationDevice = makeWebGPURenderDevice(window);
#endif
    deviceRenderer = makeDeviceRenderBackend(*presentationDevice, *deviceResources, getW(), getH());
    renderer = deviceRenderer.get(); renderer->clip(&clipRect); resetRenderPacing();
}
void GraphicContext::handleDeviceFailure(const std::string &reason)
{
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Graphics device recovery: %s", reason.c_str());
    backendStatus.reason = reason; ++backendStatus.serial;
    cancelCaptures(); transitionPixels.reset();
    // Repaint the latest immutable scene on the replacement device. A lost
    // device cannot provide a completed CPU image for the transition.
    recoveringBackend = true; nextBackendPersists = false;
    nextBackend = backendStatus.active == RendererBackend::WebGPU && programmableGLCompiled()
        ? RendererBackend::ProgrammableGL : RendererBackend::Software;
    backendStatus.phase = RendererSwitchPhase::Queued;
    applyQueuedBackend();
}

void GraphicContext::completeBackendFrame()
{
    applyQueuedBackend();
}
}

namespace GAGCore
{
std::uint64_t GraphicContext::requestMemoryCapture(CaptureCompletion complete)
{
	if (!window || !sdlsurface || pendingMemoryCaptures.size() + memoryCaptures.size() >= 8)
	{
		complete({}, 0, "No capturable frame or capture queue full");
		return 0;
	}
	const auto identity = ++memoryCaptureIdentity;
	pendingMemoryCaptures.emplace(identity, MemoryCapture{0, std::move(complete)});
	return identity;
}
void GraphicContext::cancelMemoryCapture(std::uint64_t identity)
{
	for (auto *requests : {&pendingMemoryCaptures, &memoryCaptures})
	{
		auto i = requests->find(identity);
		if (i == requests->end())
			continue;
		auto complete = std::move(i->second.complete);
		requests->erase(i);
		complete({}, 0, "Capture cancelled");
	}
}
void GraphicContext::captureMemoryFrame()
{
	if (pendingMemoryCaptures.empty())
		return;
	if (!window || !sdlsurface || nextBackend || changingBackend)
	{
		auto pending = std::move(pendingMemoryCaptures);
		pendingMemoryCaptures.clear();
		for (auto &[id, r] : pending)
			r.complete({}, 0, "No capturable frame during graphics transition");
		return;
	}
	try
	{
		if (renderer)
			renderer->flush();
		if (presentationDevice && !didRecordDeviceFrame(*renderer))
			return;
		while (!pendingMemoryCaptures.empty())
		{
			auto i = pendingMemoryCaptures.begin();
			const auto id = i->first;
			i->second.frameIdentity = captureFrameIdentity;
			// Keep the callback queued until readback admission succeeds or fails explicitly.
			if (presentationDevice)
			{
				CaptureRequest request;
				request.identity = id;
				request.frameIdentity = submittedDeviceFrameIdentity(*renderer);
				presentationDevice->outputSize(request.width, request.height);
				if (presentationDevice->capture(std::move(request)))
				{
					memoryCaptures.emplace(id, std::move(i->second));
					pendingMemoryCaptures.erase(i);
				}
				else
				{
					auto complete = std::move(i->second.complete);
					pendingMemoryCaptures.erase(i);
					complete({}, 0, "Renderer rejected capture");
				}
			}
			else
			{
				std::shared_ptr<SDL_Surface> pixels;
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
				if (!renderer && (optionFlags & USEGPU))
				{
					int w = 0, h = 0;
					SDL_GetWindowSizeInPixels(window, &w, &h);
					pixels = std::shared_ptr<SDL_Surface>(
						SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
					if (!pixels)
						throw std::runtime_error(SDL_GetError());
					GLint alignment, previousBuffer;
					glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
					glGetIntegerv(GL_READ_BUFFER, &previousBuffer);
					glPixelStorei(GL_PACK_ALIGNMENT, 1);
					glReadBuffer(GL_BACK);
#if !defined(GLOB2_WEBGL2)
					GLint rowLength;
					glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
					glPixelStorei(GL_PACK_ROW_LENGTH, pixels->pitch / 4);
#endif
					glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels->pixels);
					glPixelStorei(GL_PACK_ALIGNMENT, alignment);
					glReadBuffer(previousBuffer);
#if !defined(GLOB2_WEBGL2)
					glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
#endif
					// Normalize GL's bottom-up rows; worker PNG encoding sees top-left pixels.
					std::vector<unsigned char> row(pixels->pitch);
					auto *bytes = static_cast<unsigned char *>(pixels->pixels);
					for (int y = 0; y < h / 2; ++y)
					{
						auto *top = bytes + y * pixels->pitch,
							 *bottom = bytes + (h - y - 1) * pixels->pitch;
						std::memcpy(row.data(), top, row.size());
						std::memcpy(top, bottom, row.size());
						std::memcpy(bottom, row.data(), row.size());
					}
				}
				else
#endif
					pixels = std::shared_ptr<SDL_Surface>(
						renderer ? renderer->capture()
								 : SDL_ConvertSurface(sdlsurface, SDL_PIXELFORMAT_RGBA32),
						SDL_DestroySurface);
				auto complete = std::move(i->second.complete);
				pendingMemoryCaptures.erase(i);
				complete(pixels, captureFrameIdentity, pixels ? "" : "No capturable frame");
			}
		}
	}
	catch (const RenderAdmissionDeferred &)
	{
	}
	catch (const std::exception &error)
	{
		auto pending = std::move(pendingMemoryCaptures);
		pendingMemoryCaptures.clear();
		for (auto &[id, r] : pending)
			r.complete({}, 0, error.what());
	}
}
} // namespace GAGCore
