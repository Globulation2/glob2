// SPDX-License-Identifier: GPL-3.0-or-later
#include "RecordingFilesScreen.h"
#include <GameplayRecording.h>
#include <ApplicationHost.h>
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
		return page("Delete recording?",paragraph("The recording and its metadata will be removed."),
			actions({{"cancel","Cancel",[this] { deleting.clear(); invalidate(); }},
				{"delete","Delete",[this] { Recording::deleteFile(deleting); deleting.clear(); invalidate(); }}},p),p);
	}
	const auto files = Recording::filesStatus();
	const auto recording = Recording::recorder().status();
	const bool idle = recording.state != Recording::State::Starting && recording.state != Recording::State::Recording && recording.state != Recording::State::Finalizing;
	std::vector<Element> items;
	if (files.busy) items.push_back(paragraph("Working…"));
	if (!files.error.empty()) items.push_back(paragraph(files.error));
	if (!error.empty()) items.push_back(paragraph(error));
	if (files.files.empty() && !files.busy) items.push_back(paragraph("No recordings yet."));
	for (const auto &file : files.files)
	{
		auto value = std::filesystem::u8path(file.path).filename().u8string();
		std::string name(reinterpret_cast<const char *>(value.data()),value.size());
		std::vector<Element> entry{paragraph(name)};
		ButtonOptions enabled; enabled.enabled = !files.busy;
		if (file.recoverable)
		{
			entry.push_back(paragraph("Interrupted recording")); enabled.enabled &= idle;
			entry.push_back(button("recover/"+file.path,"Recover",[this,path=file.path] { Recording::recoverFile(path); invalidate(); },enabled));
		}
		else
		{
			for (const auto &[suffix,label] : {std::pair{"","Export video"},std::pair{".json","Export metadata"},std::pair{".events.jsonl","Export events"}})
				entry.push_back(button("export/"+file.path+suffix,label,[this,path=file.path+suffix]
				{
					if (!GAGCore::ApplicationHost::exportFilePath(path)) error = "Could not export recording.";
					invalidate();
				},enabled));
		}
		enabled.enabled &= idle;
		entry.push_back(button("delete/"+file.path,"Delete recording",[this,path=file.path] { deleting=path; invalidate(); },enabled));
		items.push_back(card(column(std::move(entry),{p.pt(6)})));
	}
	return page("Recordings",scroll("recordings/files",column(std::move(items),{p.pt(12)})),
		actions({{"refresh","Refresh",[this] { Recording::refreshFiles(); invalidate(); },!files.busy},
			{"done",tr("[done]"),[this] { endExecute(0); }}},p),p);
}
