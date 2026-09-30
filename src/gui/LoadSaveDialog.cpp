// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "LoadSaveDialog.h"
#include "GlobalContainer.h"
#include "MapHeader.h"
#include <FileManager.h>
#include <StringTable.h>
#include <TextSort.h>
#include <Toolkit.h>
#include <algorithm>
#include <map>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;

LoadSaveDialog::LoadSaveDialog(const char *directory, const char *extension, bool isLoad, std::string title,
							   const char *defaultFileName, NameFunction filenameToName, PathFunction nameToFilename)
	: isLoad(isLoad), title(std::move(title)), filenameToName(filenameToName), nameToFilename(nameToFilename)
{
	if (nameToFilename)
	{
		this->extension = extension;
		this->directory = directory;
	}
	else
	{
		this->extension = std::string(".") + extension;
		this->directory = std::string(directory) + "/";
	}
	name = defaultFileName ? defaultFileName : "";
	refresh();
	generateFileName();
}

LoadSaveDialog::~LoadSaveDialog() = default;

void LoadSaveDialog::refresh()
{
	// Both plain and gzip-compressed files list once under their display name.
	const std::string dir = nameToFilename ? directory : directory.substr(0, directory.size() - 1);
	const std::string ext = nameToFilename ? extension : extension.substr(1);
	std::map<std::string, std::string> byName;
	auto &manager = *GAGCore::Toolkit::getFileManager();
	for (const std::string &suffix : {ext, ext + ".gz"})
	{
		if (ext.empty() && suffix != ext)
			continue;
		if (!manager.initDirectoryListing(dir, suffix, false))
			continue;
		std::string filename;
		while (!(filename = manager.getNextDirectoryEntry()).empty())
		{
			if (manager.isDir(dir + "/" + filename))
				continue;
			std::string display;
			if (filenameToName)
				display = filenameToName(dir + "/" + filename);
			else
			{
				display = filename;
				for (const std::string &tail : {std::string(".gz"), "." + ext})
					if (!tail.empty() && display.size() > tail.size() && display.compare(display.size() - tail.size(), tail.size(), tail) == 0)
						display.resize(display.size() - tail.size());
			}
			if (!display.empty())
				byName[display] = filename;
		}
	}
	files.clear();
	for (const auto &entry : byName)
		files.push_back(entry.first);
	std::sort(files.begin(), files.end(), GAGCore::naturalStringSort);
	selected = -1;
	for (std::size_t i = 0; i < files.size(); ++i)
		if (files[i] == name)
			selected = int(i);
	invalidate();
}

void LoadSaveDialog::generateFileName()
{
	if (nameToFilename)
		fileName = nameToFilename(directory, name, extension);
	else
		fileName = directory + name + extension;
}

void LoadSaveDialog::setName(const std::string &value)
{
	if (persistence)
		return;
	name = value;
	generateFileName();
	selected = -1;
	for (std::size_t i = 0; i < files.size(); ++i)
		if (files[i] == name)
			selected = int(i);
	invalidate();
}

void LoadSaveDialog::selectPresentedFile(int index)
{
	if (persistence || index < 0 || index >= int(files.size()))
		return;
	selected = index;
	name = files[std::size_t(index)];
	generateFileName();
	invalidate();
}

void LoadSaveDialog::confirmPresentedFile()
{
	if (persistence || name.empty())
		return;
	generateFileName();
	finish(OK);
}

void LoadSaveDialog::cancelPresentedFile()
{
	if (persistence)
		return;
	finish(CANCEL);
}

void LoadSaveDialog::exportPresentedFile()
{
	if (persistence)
		return;
	exportSave();
}

void LoadSaveDialog::showSaveFailure()
{
	saveFailed = true;
	resume();
	status = fe::tr("[save failed retry]");
	canExport = !exportPath.empty() && GAGCore::ApplicationHost::canExportFiles();
	invalidate();
}

void LoadSaveDialog::beginPersistence(std::unique_ptr<GAGCore::ApplicationHost::Persistence> operation)
{
	saveFailed = false;
	resume();
	status = fe::tr("[saving to storage]");
	exportPath = glob2PreferGzipReadPath(*GAGCore::Toolkit::getFileManager(), fileName);
	canExport = false;
	persistence = std::move(operation);
	invalidate();
}

bool LoadSaveDialog::pollPersistence()
{
	if (!persistence)
		return false;
	const auto state = persistence->state();
	if (state == GAGCore::ApplicationHost::PersistenceState::Pending)
		return false;
	persistence.reset();
	if (state == GAGCore::ApplicationHost::PersistenceState::Failed)
	{
		showSaveFailure();
		return false;
	}
	return true;
}

void LoadSaveDialog::exportSave()
{
	if (!GAGCore::ApplicationHost::exportLocalFile(exportPath))
		showSaveFailure();
}

LoadSaveDialog::FilePresentation LoadSaveDialog::filePresentation() const
{
	FilePresentation result;
	result.title = title;
	result.status = saveFailed || persistence ? status : std::string{};
	result.name = name;
	result.load = isLoad;
	result.busy = bool(persistence);
	result.failed = saveFailed;
	result.canExport = canExport;
	result.selected = selected;
	result.files = files;
	return result;
}

Element LoadSaveDialog::build(const Presentation &p)
{
	const bool busy = bool(persistence);
	std::vector<Element> parts;
	if (!title.empty())
		parts.push_back(fe::paragraph(title, {fe::FontRole::Heading, false, fe::TextAlign::Center}));
	if (saveFailed || busy)
		parts.push_back(fe::paragraph(status, {fe::FontRole::Body, !saveFailed}));
	if (canExport)
		parts.push_back(fe::button("export", fe::tr("[export save]"), [this] { exportPresentedFile(); }));
	fe::ListOptions list;
	list.visibleRows = isLoad ? 8 : 6;
	list.emptyText = fe::tr("[No files saved yet]");
	list.activate = [this](int) { confirmPresentedFile(); };
	if (busy)
		list.enabled.assign(files.size(), false);
	parts.push_back(fe::expanded(fe::listView("files", files, selected, [this](int index) { selectPresentedFile(index); }, list)));
	if (!isLoad)
	{
		fe::TextFieldOptions entry;
		entry.maxLength = 64;
		entry.placeholder = fe::tr("[Filename]");
		entry.enabled = !busy;
		entry.submit = [this](const std::string &) { confirmPresentedFile(); };
		parts.push_back(fe::field(fe::tr("[Filename]"), fe::textField("name", name, [this](const std::string &value) { setName(value); }, entry),
								  {"", 220, true}));
	}
	std::vector<fe::MenuAction> actions;
	actions.push_back({"ok", fe::tr("[ok]"), [this] { confirmPresentedFile(); }, true, SDLK_RETURN, !busy && !name.empty()});
	actions.push_back({"cancel", fe::tr("[Cancel]"), [this] { cancelPresentedFile(); }, false, SDLK_ESCAPE, !busy});
	return fe::footer(fe::column(std::move(parts), {p.pt(10)}), fe::actions(std::move(actions), p));
}
