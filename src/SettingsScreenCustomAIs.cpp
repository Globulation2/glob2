// SPDX-License-Identifier: GPL-3.0-or-later
#include "SettingsScreen.h"
#include "ai/ScriptLibrary.h"

namespace AH = GAGCore::ApplicationHost;
struct SettingsScreen::CustomAIState
{
	std::unique_ptr<Online::OnlineStorage> storage = Online::makeUserDirectoryStorage();
	Script::Library library{*storage};
	std::unique_ptr<AH::FileSelection> picker;
	std::unique_ptr<AH::Persistence> persistence;
	std::string replace, before, notice;
	std::map<std::string, std::string> errors;
	bool linked = false;
};
void SettingsScreen::selectCustomAIFile(bool linked, const std::string &replace)
{
	if (!customAIs || customAIs->picker || customAIs->persistence)
		return;
	customAIs->replace = replace;
	customAIs->linked = linked;
	customAIs->picker = AH::selectFile("js");
	if (!customAIs->picker)
		customAIs->notice = "File selection is unavailable.";
	invalidate();
}
void SettingsScreen::pollCustomAIs()
{
	if (!customAIs)
		return;
	auto &s = *customAIs;
	try
	{
		if (s.picker && s.picker->state() != AH::FileSelectionState::Pending)
		{
			auto picker = std::move(s.picker);
			if (picker->state() == AH::FileSelectionState::Selected)
			{
				auto file = picker->takeFile();
				if (s.linked && file.externalPath.empty())
					throw std::runtime_error("External links require a desktop file path");
				s.before = s.library.checkpoint();
				s.library.put(std::string(file.bytes.begin(), file.bytes.end()), file.name,
							  s.replace, s.linked ? file.externalPath : "");
				s.persistence = AH::persistStorage();
				s.notice = "Saving custom AI…";
			}
			else if (picker->state() == AH::FileSelectionState::Failed)
				s.notice = "Could not read the selected JavaScript file.";
			invalidate();
		}
		if (s.persistence && s.persistence->state() != AH::PersistenceState::Pending)
		{
			const bool ok = s.persistence->state() == AH::PersistenceState::Succeeded;
			s.persistence.reset();
			if (!ok)
				s.library.rollback(s.before);
			else
			{
				s.library.collectUnusedSources();
				s.errors.erase(s.replace);
			}
			s.notice = ok ? "Custom AI library saved."
						  : "Storage could not be saved. The previous library is retained.";
			invalidate();
		}
	}
	catch (const std::exception &e)
	{
		s.notice = e.what();
		invalidate();
	}
}
void SettingsScreen::buildCustomAIs()
{
	try
	{
		if (!customAIs)
			customAIs = std::make_shared<CustomAIState>();
	}
	catch (const std::exception &e)
	{
		info(e.what());
		return;
	}
	auto &s = *customAIs;
	info("Import a single JavaScript AI file, then select it when setting up a local game.");
	if (!s.notice.empty())
		info(s.notice);
	if (s.picker || s.persistence)
	{
		info("Waiting for file selection or storage…");
		return;
	}
	button("ai.import", "Import JavaScript AI", [this] { selectCustomAIFile(false); });
#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
	button("ai.link", "Link development file", [this] { selectCustomAIFile(true); });
	info("Linked files are read at game start. Rebuild your file before starting a new game; "
		 "running games keep their original source.");
#endif
	button("ai.example", "Example AI and authoring guide",
		   [] { AH::openUrl("https://github.com/Globulation2/javascript-ai-example"); });
	for (const auto &entry : s.library.entries())
	{
		const auto id = entry.id;
		info(entry.metadata.name + " · #" + entry.id +
			 (entry.metadata.version.empty() ? "" : " · " + entry.metadata.version) +
			 (entry.linked ? " · Linked" : " · Imported"));
		if (s.errors.contains(id))
			info(s.errors[id]);
		if (!entry.metadata.description.empty())
			info(entry.metadata.description);
		if (entry.linked)
			info(entry.path);
		button("ai.update." + id, entry.linked ? "Relink" : "Update",
			   [this, id, linked = entry.linked] { selectCustomAIFile(linked, id); });
		button("ai.validate." + id, "Validate",
			   [this, id]
			   {
				   try
				   {
					   customAIs->library.configuration(id);
					   customAIs->errors.erase(id);
					   customAIs->notice = "AI startup and persistent globals are valid.";
				   }
				   catch (const std::exception &e)
				   {
					   customAIs->notice = e.what();
					   customAIs->errors[id] = e.what();
				   }
				   invalidate();
			   });
		button("ai.remove." + id, "Remove from library",
			   [this, id]
			   {
				   try
				   {
					   customAIs->before = customAIs->library.checkpoint();
					   customAIs->library.remove(id);
					   customAIs->persistence = AH::persistStorage();
				   }
				   catch (const std::exception &e)
				   {
					   customAIs->notice = e.what();
					   customAIs->errors[id] = e.what();
				   }
				   invalidate();
			   });
	}
}

bool SettingsScreen::customAIBusy() const
{
	return customAIs && (customAIs->picker || customAIs->persistence);
}
