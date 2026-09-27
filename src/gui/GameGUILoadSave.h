// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <GUIBase.h>
#include <ApplicationHost.h>
using namespace GAGGUI;
#include "GameGUIDialog.h"
#include <string>
#include <vector>

namespace GAGGUI
{
class List;
class TextInput;
class Text;
class TextButton;
} // namespace GAGGUI

class LoadSaveScreen : public OverlayScreen
{
  public:
	enum
	{
		OK = 0,
		CANCEL = 1,
		EXPORT = 2
	};

  private:
	List *fileList;
	Text *caption;
	TextButton *exportButton;
	std::string exportPath;
	std::string originalTitle;
	bool saveFailed = false;
	void exportSave();
	std::unique_ptr<GAGCore::ApplicationHost::Persistence> persistence;
	TextInput *fileNameEntry;
	bool isLoad;
	std::string extension;
	std::string directory;
	std::string fileName;
	std::string (*filenameToNameFunc)(const std::string &filename);
	std::string (*nameToFilenameFunc)(const std::string &dir, const std::string &name,
									  const std::string &extension);

  private:
	//! create a filename from user friendly's name
	void generateFileName(void);

  public:
	//! Constructor.
	//! \a directory and \a extension are given without the trailing '/' and '.'.
	//! \a title is the localized window caption; the caller picks it (the dialog
	//! has no knowledge of load-vs-save or game-vs-script — it just shows the string).
	//! In load mode the filename text entry is hidden, so the file list is laid out
	//! taller (175 px) than in save mode (140 px); this is the only layout difference
	//! between the two modes.
	LoadSaveScreen(const char *directory, const char *extension, bool isLoad = true,
				   std::string title = "", const char *defaultFileName = NULL,
				   std::string (*filenameToNameFunc)(const std::string &filename) = NULL,
				   std::string (*nameToFilenameFunc)(const std::string &dir,
													 const std::string &name,
													 const std::string &extension) = NULL);
	virtual ~LoadSaveScreen();
	void showSaveFailure();
	void beginPersistence(std::unique_ptr<GAGCore::ApplicationHost::Persistence> operation);
	bool pollPersistence();
	bool isPersisting() const { return bool(persistence); }
	// Purpose-built presentations consume data and named actions, never the
	// desktop dialog's coordinates or its composed widget tree.
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
	TextInput *nameInput() const { return fileNameEntry; }
	void editName(SDL_Event event);

	virtual void onAction(Widget *source, Action action, int par1, int par2);
	virtual void onSDLEvent(SDL_Event *event);
	const char *getFileName(void);
	const char *getName(void);
};
