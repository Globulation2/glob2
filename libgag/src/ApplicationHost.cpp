// SPDX-License-Identifier: GPL-3.0-or-later
#include <ApplicationHost.h>
#include <EventQueue.h>
#include <BrowserTextInput.h>
#include <GraphicContext.h>
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <mutex>
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
		GAGCore::EventQueue events;
		SDL_Event event;
		while (GraphicContext::pollEvent(&event))
			events.push_back(event);
		if (!loop->frame(SDL_GetTicks(), events.events()))
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
void initializeOpenGLContext() {}
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
namespace
{
struct DesktopSelectionState
{
	std::mutex mutex;
	FileSelectionState state = FileSelectionState::Pending;
	SelectedFile file;
	std::string extension;
	SDL_DialogFileFilter filter{};
};
class DesktopSelection : public FileSelection
{
	std::shared_ptr<DesktopSelectionState> shared = std::make_shared<DesktopSelectionState>();

  public:
	explicit DesktopSelection(const std::string &extension)
	{
		auto *lifetime = new std::shared_ptr<DesktopSelectionState>(shared);
		shared->extension = extension;
		shared->filter = {"Selected files", shared->extension.c_str()};
		SDL_ShowOpenFileDialog(
			[](void *opaque, const char *const *files, int)
			{
				std::unique_ptr<std::shared_ptr<DesktopSelectionState>> lifetime(
					static_cast<std::shared_ptr<DesktopSelectionState> *>(opaque));
				auto &s = **lifetime;
				std::lock_guard lock(s.mutex);
				s.state = FileSelectionState::Failed;
				if (!files)
					return;
				if (!files[0])
				{
					s.state = FileSelectionState::Cancelled;
					return;
				}
				try
				{
					std::filesystem::path path =
						std::filesystem::absolute(std::filesystem::u8path(files[0]));
					std::ifstream input(path, std::ios::binary);
					if (!input)
						return;
					SelectedFile selected;
					const auto name = path.filename().u8string(), location = path.u8string();
					selected.name.assign(name.begin(), name.end());
					selected.externalPath.assign(location.begin(), location.end());
					char bytes[8192];
					while (input.read(bytes, sizeof(bytes)) || input.gcount())
					{
						if (selected.bytes.size() + input.gcount() > 64 * 1024 * 1024)
							return;
						selected.bytes.insert(selected.bytes.end(), bytes, bytes + input.gcount());
					}
					if (!input.eof())
						return;
					s.file = std::move(selected);
					s.state = FileSelectionState::Selected;
				}
				catch (...)
				{
					s.state = FileSelectionState::Failed;
				}
			},
			lifetime, nullptr, &shared->filter, 1, nullptr, false);
	}
	FileSelectionState state() const override
	{
		std::lock_guard lock(shared->mutex);
		return shared->state;
	}
	SelectedFile takeFile() override
	{
		std::lock_guard lock(shared->mutex);
		return std::move(shared->file);
	}
};
} // namespace
bool canImportFiles()
{
	return true;
}
std::unique_ptr<FileSelection> selectFile(const std::string &extension)
{
	return std::make_unique<DesktopSelection>(extension);
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
// Native and mobile builds ship every data file with the application.
bool assetPackageReady(const char *) { return true; }
std::vector<std::string> takeInstalledAssetPackages() { return {}; }

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
bool openUrl(const std::string &url)
{
	if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0)
		return false;
	return SDL_OpenURL(url.c_str());
}
bool copyText(const std::string &text)
{
	return SDL_SetClipboardText(text.c_str());
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
void customGameReady(bool) {}
void controlsChanged(const void *, const char *) {}
bool controlsObserved() { return false; }
void exited(int) {}
} // namespace GAGCore::ApplicationHost

namespace GAGCore {
void forgetBrowserTextInput(const void*) {}
void focusBrowserTextInput(const void*) {}
bool hasBrowserTextInput(const void*) { return false; }
void beginBrowserTextFrame() {}
void endBrowserTextFrame() {}
void browserTextInput(const void*,SDL_Rect,int,int,const std::string&,bool,size_t,BrowserTextChange,const SDL_Rect*,bool) {}
}
