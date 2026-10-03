// SPDX-License-Identifier: GPL-3.0-or-later
// Settings > Recording: the recording controls, their hotkey and whether FFmpeg works.
#include "SettingsScreen.h"
#include "GameGUIKeyActions.h"
#include "ui/RecordingControls.h"
#include <FileManager.h>
#include <FormatableString.h>
#include <Toolkit.h>
#include <filesystem>

using namespace Glob2UI;

void SettingsScreen::buildRecording()
{
	namespace Rec = GAGCore::Recording;
	const auto encoder = Rec::encoder();
	recordingEncoder = int(encoder);
	info(tr("Record your games as MP4 videos with sound. Recording needs FFmpeg with the libx264 and AAC encoders."));
	if (encoder == Rec::Encoder::Available)
		info(tr("FFmpeg is ready."));
	else if (encoder == Rec::Encoder::Missing)
	{
		const std::string problem = Rec::encoderProblem();
		custom("", [this, problem](const Presentation &p)
			   {
				   TextOptions warning;
				   warning.color = theme().palette.warning;
				   std::vector<Element> parts{paragraph(tr("FFmpeg is not available, so recording is turned off. Install FFmpeg and make sure it is on your PATH, then check again."), warning)};
				   if (!problem.empty())
					   parts.push_back(paragraph(problem, {FontRole::Support, true}));
				   return column(std::move(parts), {p.pt(2)});
			   });
		button("recording.check", tr("Check for FFmpeg again"), [] { Rec::probeEncoder(true); });
	}
	else
		info(tr("Checking for FFmpeg…"));
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
	const auto folder = std::filesystem::u8path(GAGCore::Toolkit::getFileManager()->getDir(0)) / "videoshots";
	const auto folderText = folder.u8string();
	info(std::string(GAGCore::FormattableString(tr("Recordings are saved in %0")).arg(std::string(folderText.begin(), folderText.end()))));
}
