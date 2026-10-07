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
	const char *recordingId() const override { return "load_save_dialog"; }
	enum
	{
		OK = 0,
		CANCEL = 1,
		EXPORT = 2,
		// "From device…" was chosen; the owner opens the host file picker and
		// resume()s the dialog to show its outcome.
		DEVICE = 3
	};
	using NameFunction = std::string (*)(const std::string &filename);
	using PathFunction = std::string (*)(const std::string &dir, const std::string &name, const std::string &extension);

	//! \a directory and \a extension are given without the trailing '/' and '.'.
	//! \a title is the localized caption; in load mode there is no name entry.
	//! Disable \a includeGzip when the caller accepts only uncompressed files.
	LoadSaveDialog(const char *directory, const char *extension, bool isLoad = true,
				   std::string title = "", const char *defaultFileName = nullptr,
				   NameFunction filenameToName = nullptr, PathFunction nameToFilename = nullptr,
				   Glob2UI::Surface surface = Glob2UI::Surface::Match, bool includeGzip = true);
	~LoadSaveDialog() override;
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;

	void showSaveFailure();
	void showLoadFailure(const std::string &message);
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
		// Saving over an existing file waits for confirmOverwrite().
		bool confirmingOverwrite = false;
		bool deviceImport = false;
	};
	FilePresentation filePresentation() const;
	void selectPresentedFile(int index);
	void confirmPresentedFile();
	void cancelPresentedFile();
	void exportPresentedFile();
	void setName(const std::string &value);
	// Saving to an existing name asks first, except for this name (the file the
	// caller's document was loaded from or last saved to).
	void allowOverwriteOf(const std::string &value) { ownName = value; }
	void confirmOverwrite();
	void cancelOverwrite();
	// Offer "From device…" beside the list (load mode).
	void enableDeviceImport() { deviceImport = true; invalidate(); }
	void chooseDevice();
	// Text shown when the directory has no files.
	void setEmptyText(std::string value) { emptyText = std::move(value); invalidate(); }
	// A neutral status line (not a failure).
	void showNotice(const std::string &message);
	// Rescan the directory (after a save, say).
	void refresh();
	const char *getFileName() const { return fileName.c_str(); }
	const char *getName() const { return name.c_str(); }

  protected:
	void onEscape() override { if (confirmingOverwrite) cancelOverwrite(); else cancelPresentedFile(); }
	double maxWidth() const override { return -1; }

  private:
	bool isLoad, includeGzip;
	std::string title, extension, directory, name, fileName, status, exportPath, ownName, emptyText;
	NameFunction filenameToName;
	PathFunction nameToFilename;
	std::vector<std::string> files;
	int selected = -1;
	bool saveFailed = false, canExport = false, confirmingOverwrite = false, deviceImport = false, notice = false;
	std::unique_ptr<GAGCore::ApplicationHost::Persistence> persistence;
	void generateFileName();
	void exportSave();
};
