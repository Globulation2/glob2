// SPDX-License-Identifier: GPL-3.0-or-later
#include "RecordingFilesScreen.h"
#include "ui/RecordingNotice.h"
#include <GameplayRecording.h>
#include <ApplicationHost.h>
#include <FormatableString.h>
#include <filesystem>

using namespace Glob2UI;
namespace Recording = GAGCore::Recording;
RecordingFilesScreen::RecordingFilesScreen() { Recording::refreshFiles(); }
void RecordingFilesScreen::beforePaint()
{
	if (SDL_GetTicks()-refreshed >= 250) { refreshed = SDL_GetTicks(); invalidate(); }
}
Element RecordingFilesScreen::build(const Presentation &p)
{
	if (!deleting.empty())
	{
		return page(tr("[recording delete confirm]"),paragraph(tr("[recording delete warning]")),
			actions({{"cancel",tr("[Cancel]"),[this] { deleting.clear(); invalidate(); }},
				{"delete",tr("[recording delete]"),[this] { Recording::deleteFile(deleting); deleting.clear(); invalidate(); }}},p),p);
	}
	const auto files = Recording::filesStatus();
	const auto recording = Recording::recorder().status();
	const bool idle = recording.state != Recording::State::Starting && recording.state != Recording::State::Recording && recording.state != Recording::State::Finalizing;
	std::vector<Element> items;
	// Where the files are comes first: the folder, with a way to open it, on
	// desktop; the browser's own storage and its downloads on the web.
	if (recordingsDownload())
		items.push_back(hint(tr("[recording browser storage]")));
	else
	{
		const auto folder = Recording::recordingDirectory();
		items.push_back(hint(GAGCore::FormattableString(tr("[settings Recordings are saved in %0]")).arg(folder)));
		if (GAGCore::ApplicationHost::canOpenFolder())
			items.push_back(button("recordings/open-folder",tr("[recording open folder]"),[this,folder]
			{
				if (!GAGCore::ApplicationHost::openFolder(folder)) error = tr("[recording open folder failed]");
				invalidate();
			}));
	}
	if (files.busy) items.push_back(paragraph(tr("[recording working]")));
	if (!files.error.empty()) items.push_back(paragraph(filesError(files.error)));
	if (!error.empty()) items.push_back(paragraph(error));
	if (files.files.empty() && !files.busy) items.push_back(paragraph(tr("[recording none]")));
	for (const auto &file : files.files)
	{
		auto value = std::filesystem::u8path(file.path).filename().u8string();
		std::string name(reinterpret_cast<const char *>(value.data()),value.size());
		std::vector<Element> entry{paragraph(name)};
		const auto details = recordingFileDetails(file);
		if (!details.empty()) entry.push_back(caption(details));
		std::vector<Element> buttons;
		ButtonOptions enabled; enabled.enabled = !files.busy;
		if (file.recoverable)
		{
			entry.push_back(paragraph(tr("[recording interrupted]"))); enabled.enabled &= idle;
			buttons.push_back(button("recover/"+file.path,tr("[recording recover]"),[this,path=file.path] { Recording::recoverFile(path); invalidate(); },enabled));
		}
		else
		{
			ButtonOptions primary = enabled; primary.primary = true;
			const char *videoLabel = recordingsDownload() ? "[recording download]" : "[recording export video]";
			for (const auto &[suffix,label] : {std::pair{"",videoLabel},std::pair{".json","[recording export metadata]"},std::pair{".events.jsonl","[recording export events]"}})
				buttons.push_back(button("export/"+file.path+suffix,tr(label),[this,path=file.path+suffix]
				{
					if (!GAGCore::ApplicationHost::exportFilePath(path)) error = tr("[recording export failed]");
					invalidate();
				},*suffix ? enabled : primary));
		}
		enabled.enabled &= idle;
		buttons.push_back(button("delete/"+file.path,tr("[recording delete action]"),[this,path=file.path] { deleting=path; invalidate(); },enabled));
		WrapOptions layout; layout.gap = p.pt(6); layout.minChildWidth = p.pt(120);
		entry.push_back(wrap(std::move(buttons),layout));
		items.push_back(card(column(std::move(entry),{p.pt(6)})));
	}
	return page(tr("[recordings]"),scroll("recordings/files",column(std::move(items),{p.pt(12)})),
		actions({{"refresh",tr("[recording refresh]"),[this] { Recording::refreshFiles(); invalidate(); },!files.busy},
			{"done",tr("[Done]"),[this] { endExecute(0); }}},p),p);
}
