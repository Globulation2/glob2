// SPDX-License-Identifier: GPL-3.0-or-later
#include "MusicUI.h"
#include "MusicImportScreen.h"
#include "ui/OnlineUI.h"
#include <Toolkit.h>
#include <FileManager.h>
using namespace Glob2UI;
namespace AH = GAGCore::ApplicationHost;
MusicImportScreen::MusicImportScreen()
	: library(std::filesystem::u8path(GAGCore::Toolkit::getFileManager()->getDir(0)))
{
}
void MusicImportScreen::choose(int slot)
{
	selected = slot;
	picker = AH::selectFile(slot < 0 ? "zip" : "opus");
	notice = musicText("Choose a music file.");
	invalidate();
}
void MusicImportScreen::install()
{
	try
	{
		job = selected < 0 ? std::make_unique<Music::ImportJob>(library, archive)
						   : std::make_unique<Music::ImportJob>(library, tracks);
		notice = musicText("Validating music…");
	}
	catch (const std::exception &error)
	{
		notice = error.what();
	}
	invalidate();
}
void MusicImportScreen::onTimer(Uint32)
{
	if (job)
	{
		job->advance();
		if (job->finished())
		{
			if (job->error().empty())
			{
				notice = musicText("Saving music…");
				persistence = AH::persistStorage();
			}
			else
				notice = job->error();
			job.reset();
			invalidate();
		}
	}
	if (picker && picker->state() != AH::FileSelectionState::Pending)
	{
		if (picker->state() == AH::FileSelectionState::Selected)
		{
			auto file = picker->takeFile();
			if (selected < 0)
				archive = std::move(file.bytes);
			else
				tracks[selected] = std::move(file.bytes);
			notice = musicText("Ready to import.");
		}
		else
			notice = musicText("File selection cancelled or failed.");
		picker.reset();
		invalidate();
	}
	if (persistence && persistence->state() != AH::PersistenceState::Pending)
	{
		failedPersistence = persistence->state() == AH::PersistenceState::Failed;
		notice =
			musicText(failedPersistence
						  ? "Storage could not be saved. Retry or export your files before leaving."
						  : "Music installed. It is now available in Audio settings.");
		persistence.reset();
		if (!failedPersistence)
		{
			archive.clear();
			for (auto &file : tracks)
				file.clear();
		}
		invalidate();
	}
}
void MusicImportScreen::onEscape()
{
	if (!persistence && !failedPersistence)
	{
		job.reset();
		endExecute(0);
	}
}
Element MusicImportScreen::build(const Presentation &p)
{
	std::vector<Element> body{
		paragraph(musicText("Import a downloaded music ZIP or the three Opus tracks. Metadata and "
							"cover art are read from Calm; no extra files are required."))};
	if (!picker && !persistence && !job && !failedPersistence)
	{
		body.push_back(button("music.zip", musicText("Choose ZIP"), [this] { choose(-1); }));
		const char *names[] = {"Choose Calm", "Choose Building", "Choose Combat"};
		for (int i = 0; i < 3; ++i)
			body.push_back(button("music.file." + std::to_string(i),
								  musicText(names[i]) + (tracks[i].empty() ? "" : " ✓"),
								  [this, i] { choose(i); }));
		if (!archive.empty())
			body.push_back(button("music.import.zip", musicText("Import ZIP"),
								  [this]
								  {
									  selected = -1;
									  install();
								  }));
		if (!tracks[0].empty() && !tracks[1].empty() && !tracks[2].empty())
			body.push_back(button("music.import.trio", musicText("Import three tracks"),
								  [this]
								  {
									  selected = 0;
									  install();
								  }));
	}
	if (failedPersistence)
	{
		body.push_back(button("music.retry", musicText("Retry saving"),
							  [this] { persistence = AH::persistStorage(); }));
		body.push_back(button("music.leave", musicText("Leave without saving"),
							  [this]
							  {
								  failedPersistence = false;
								  onEscape();
							  }));
		body.push_back(button("music.export", musicText("Export recovery files"),
							  [this]
							  {
								  if (!archive.empty())
									  AH::exportFile("glob2-music.zip", archive);
								  else
									  for (int i = 0; i < 3; ++i)
										  AH::exportFile("a" + std::to_string(i + 1) + ".opus",
														 tracks[i]);
							  }));
	}
	body.push_back(paragraph(notice));
	OnlinePanel panel;
	panel.title = musicText("Import music");
	panel.body = scroll("music.import", column(std::move(body), {p.pt(12)}));
	panel.actions = {{"back", musicText("Back"), [this] { onEscape(); }, false, SDLK_ESCAPE}};
	return onlinePanel(panel, p);
}
