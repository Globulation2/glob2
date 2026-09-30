// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#pragma once
#include "ui/FrontendUI.h"
#include <ApplicationHost.h>
#include <memory>
#include <string>
#include <vector>

// Pick a file to load, or name one to save, from one virtual directory. The
// caller reads getFileName() after OK; a save that fails or is still being
// persisted keeps the dialog open with a status line until it succeeds.
class LoadSaveDialog : public Glob2UI::InGameDialog
{
  public:
	enum
	{
		OK = 0,
		CANCEL = 1,
		EXPORT = 2
	};
	using NameFunction = std::string (*)(const std::string &filename);
	using PathFunction = std::string (*)(const std::string &dir, const std::string &name, const std::string &extension);

	//! \a directory and \a extension are given without the trailing '/' and '.'.
	//! \a title is the localized caption; in load mode there is no name entry.
	LoadSaveDialog(const char *directory, const char *extension, bool isLoad = true, std::string title = "",
				   const char *defaultFileName = nullptr, NameFunction filenameToName = nullptr,
				   PathFunction nameToFilename = nullptr);
	~LoadSaveDialog() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;

	void showSaveFailure();
	void beginPersistence(std::unique_ptr<GAGCore::ApplicationHost::Persistence> operation);
	// True once a pending save has completed; a failure reopens the retry state.
	bool pollPersistence();
	bool isPersisting() const { return bool(persistence); }
	// Data view of the dialog for harnesses and captures.
	struct FilePresentation
	{
		std::string title, status, name;
		std::vector<std::string> files;
		int selected = -1;
		bool load = true, busy = false, failed = false, canExport = false;
	};
	FilePresentation filePresentation() const;
	void selectPresentedFile(int index);
	void confirmPresentedFile();
	void cancelPresentedFile();
	void exportPresentedFile();
	void setName(const std::string &value);
	// Rescan the directory (after a save, say).
	void refresh();
	const char *getFileName() const { return fileName.c_str(); }
	const char *getName() const { return name.c_str(); }

  protected:
	void onEscape() override { cancelPresentedFile(); }
	double maxWidth() const override { return classic() ? 280 : -1; }

  private:
	bool isLoad;
	std::string title, extension, directory, name, fileName, status, exportPath;
	NameFunction filenameToName;
	PathFunction nameToFilename;
	std::vector<std::string> files;
	int selected = -1;
	bool saveFailed = false, canExport = false;
	std::unique_ptr<GAGCore::ApplicationHost::Persistence> persistence;
	void generateFileName();
	void exportSave();
};
