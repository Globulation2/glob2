// SPDX-License-Identifier: GPL-3.0-or-later
// Settings > Recording: embedded recording controls, hotkey and output location.
#include "SettingsScreen.h"
#include "GameGUIKeyActions.h"
#include "ui/RecordingControls.h"
#include "ui/RecordingNotice.h"
#include <ApplicationHost.h>
#include <FileManager.h>
#include <FormatableString.h>
#include <Toolkit.h>
#include <filesystem>

using namespace Glob2UI;

void SettingsScreen::buildRecording()
{
	namespace Rec = GAGCore::Recording;
	recordingState = int(Rec::recorder().status().state);
	info(tr("Record your games as MP4 videos with sound."));
	info(tr("Recording uses the full rendered resolution at 30 FPS."));
	custom("recording/toggle", [](const Presentation &) { return recordingControls(true); });

	std::string hotkey;
	for (const auto &shortcut : gameKeys.getKeyboardShortcuts())
		if (shortcut.getAction() == GameGUIKeyActions::ToggleRecording)
			hotkey += (hotkey.empty() ? "" : ", ") + bindingLabel(shortcut);
	if (hotkey.empty())
		hotkey = tr("Unbound");
	info(std::string(GAGCore::FormattableString(tr("Hotkey: %0. It also works in menus and the editor.")).arg(hotkey)));
	button("recording.keys", tr("Change the hotkey"), [this]
		   {
			   shortcutMode = GameGUIShortcuts;
			   selectCategory(Category::Controls);
		   });
	// The browser's virtual path means nothing to a player; its recordings download.
	if (recordingsDownload())
	{
		info(Glob2UI::tr("[recording browser storage]"));
		return;
	}
	const auto folder = Rec::recordingDirectory();
	info(std::string(GAGCore::FormattableString(tr("Recordings are saved in %0")).arg(folder)));
	if (GAGCore::ApplicationHost::canOpenFolder())
		button("recording.folder", Glob2UI::tr("[recording open folder]"), [folder]
			   { GAGCore::ApplicationHost::openFolder(folder); });
}
