// SPDX-License-Identifier: GPL-3.0-or-later
#include <ApplicationHost.h>
#include <BrowserTextInput.h>
#ifndef YOG_SERVER_ONLY
#include <GraphicContext.h>
#endif
#include <SDL.h>
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#ifdef GLOB2_MOBILE
#include "../../mobile/Documents.h"
#include <Toolkit.h>
#include <StringTable.h>
#endif

namespace GAGCore::ApplicationHost
{
void run(std::unique_ptr<Loop> loop, std::function<void()> complete)
{
	for (;;)
	{
		std::vector<SDL_Event> events;
		SDL_Event event;
#ifdef YOG_SERVER_ONLY
		while (SDL_PollEvent(&event))
#else
		while (GraphicContext::pollEvent(&event))
#endif
			events.push_back(event);
		if (!loop->frame(SDL_GetTicks(), events))
			break;
		wait(loop->delay(SDL_GetTicks()));
	}
	loop.reset();
	complete();
}

void wait(std::uint32_t milliseconds)
{
	if (milliseconds)
		SDL_Delay(milliseconds);
}
bool takeVisibilityChange(bool &)
{
	return false;
}
bool presentationMetrics(ViewportMetrics&,InputCapabilities&) { return false; }
bool takeViewportSize(int &, int &)
{
	return false;
}
#ifdef GLOB2_MOBILE
bool canImportFiles() { return true; }
std::unique_ptr<FileSelection> selectFile(const std::string &extension) { return MobileDocuments::select(extension); }
bool canExportFiles() { return true; }
bool exportFile(const std::string &name, const std::vector<unsigned char> &bytes) {
    return MobileDocuments::exportFile(name, bytes, Toolkit::getStringTable()->getString("[export failed]"));
}
#else
bool canImportFiles()
{
	return false;
}
std::unique_ptr<FileSelection> selectFile(const std::string &)
{
	return {};
}
bool canExportFiles()
{
	return false;
}
bool exportFile(const std::string &, const std::vector<unsigned char> &)
{
	return false;
}
#endif
bool storageRestoreFailed() { return false; }

namespace
{
class NativePersistence : public Persistence
{
	PersistenceState state() const override { return PersistenceState::Succeeded; }
};
} // namespace
std::unique_ptr<Persistence> persistStorage()
{
	return std::make_unique<NativePersistence>();
}
void importChanged(const char *) {}
void screenChanged(const char *name) {
#ifdef GLOB2_MOBILE
    SDL_Log("Glob2 screen ready: %s", name);
#endif
}
void simulationAdvanced(std::uint32_t) {}
void matchFrame(bool) {}
void overviewDrawn(bool) {}
void roomReady(bool) {}
void exited(int) {}
} // namespace GAGCore::ApplicationHost

namespace GAGCore {
void forgetBrowserTextInput(const void*) {}
bool hasBrowserTextInput(const void*) { return false; }
void beginBrowserTextFrame() {}
void endBrowserTextFrame() {}
void browserTextInput(const void*,SDL_Rect,int,int,const std::string&,bool,size_t,BrowserTextChange,const SDL_Rect*) {}
}
