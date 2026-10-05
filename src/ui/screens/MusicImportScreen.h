// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include "MusicLibrary.h"
#include <ApplicationHost.h>
class MusicImportScreen : public Glob2UI::Screen
{
  public:
	MusicImportScreen();
	Glob2UI::Element build(const Glob2UI::Presentation &) override;
	void onTimer(Uint32) override;
	const char *recordingId() const override { return "music_import"; }

  protected:
	bool panel() const override { return false; }
	void onEscape() override;

  private:
	Music::Library library;
	std::unique_ptr<Music::ImportJob> job;
	std::unique_ptr<GAGCore::ApplicationHost::FileSelection> picker;
	std::unique_ptr<GAGCore::ApplicationHost::Persistence> persistence;
	std::array<std::vector<unsigned char>, 3> tracks;
	std::vector<unsigned char> archive;
	int selected = -1;
	bool failedPersistence = false;
	std::string notice;
	void choose(int slot);
	void install();
};
