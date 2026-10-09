#include "OnlineGeneratorsScreen.h"
#include <ScreenStack.h>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SettingsScreen.h"
#include "GeneratorPackage.h"

namespace AH = GAGCore::ApplicationHost;
namespace JSGen = MapGeneration::JavaScript;
struct SettingsScreen::CustomGeneratorState
{
	std::unique_ptr<Online::OnlineStorage> storage = Online::makeUserDirectoryStorage();
	JSGen::Library library{*storage};
	std::unique_ptr<AH::FileSelection> picker;
	std::unique_ptr<AH::Persistence> persistence;
	std::string replace, before, notice;

	// Disk replacement happens in Library; publication waits for host persistence
	// (asynchronous IndexedDB synchronization on browsers). Both mutation paths use
	// this transition so unavailable persistence restores the same checkpoint.
	template <typename Mutation> void beginSave(Mutation mutation)
	{
		before = library.checkpoint();
		mutation(library);
		try
		{
			persistence = AH::persistStorage();
			if (!persistence)
				throw std::runtime_error("Generator storage persistence is unavailable");
		}
		catch (...)
		{
			library.rollback(before);
			throw;
		}
	}
};
bool SettingsScreen::customGeneratorBusy() const
{
	return customGenerators && (customGenerators->picker || customGenerators->persistence);
}
void SettingsScreen::pollCustomGenerators()
{
	if (!customGenerators)
		return;
	auto &s = *customGenerators;
	try
	{
		if (s.picker && s.picker->state() != AH::FileSelectionState::Pending)
		{
			auto picker = std::move(s.picker);
			if (picker->state() == AH::FileSelectionState::Selected)
			{
				auto file = picker->takeFile();
				s.beginSave(
					[&](JSGen::Library &library)
					{ library.put(std::string(file.bytes.begin(), file.bytes.end()), s.replace); });
				s.notice = tr("Saving generator library…");
			}
			else if (picker->state() == AH::FileSelectionState::Failed)
				s.notice = tr("Could not read generator package.");
			invalidate();
		}
		if (s.persistence && s.persistence->state() != AH::PersistenceState::Pending)
		{
			bool ok = s.persistence->state() == AH::PersistenceState::Succeeded;
			s.persistence.reset();
			if (ok)
				s.library.publish();
			else
				s.library.rollback(s.before);
			s.notice = ok ? tr("Generator library saved.")
						  : tr("Storage could not be saved. The previous library is retained.");
			invalidate();
		}
	}
	catch (const std::exception &error)
	{
		s.notice = error.what();
		invalidate();
	}
}
void SettingsScreen::buildCustomGenerators()
{
	try
	{
		if (!customGenerators)
			customGenerators = std::make_shared<CustomGeneratorState>();
	}
	catch (const std::exception &error)
	{
		info(error.what());
		return;
	}
	auto &s = *customGenerators;
	info(tr("Import a generator package, then select its landscape when creating a map or game."));
	if (!s.notice.empty())
		info(s.notice);
	if (customGeneratorBusy())
	{
		info(tr("Waiting for file selection or storage…"));
		return;
	}
	if (screens)
		button("generator.browse", tr("Browse shared generators"),
			   [this]
			   {
				   screens->push(std::make_unique<OnlineGeneratorsScreen>(*screens),
								 [this](GAGGUI::Screen &, int)
								 {
									 customGenerators.reset();
									 invalidate();
								 });
			   });
	button("generator.import", tr("Import map generator"),
		   [this]
		   {
			   customGenerators->replace.clear();
			   customGenerators->picker = AH::selectFile("json");
			   if (!customGenerators->picker)
				   customGenerators->notice = tr("File selection is unavailable.");
			   invalidate();
		   });
	if (s.library.entries().empty())
		info(tr("No custom map generators installed."));
	for (const auto &[id, p] : s.library.entries())
	{
		add("", Kind::Section, p->name + " · " + id + " · " + std::to_string(p->revision));
		if (!p->description.empty())
			info(p->description);
		button("generator.update." + id, tr("Replace package"),
			   [this, id]
			   {
				   customGenerators->replace = id;
				   customGenerators->picker = AH::selectFile("json");
				   invalidate();
			   });
		button("generator.export." + id, tr("Export package"),
			   [this, p]
			   {
				   if (!AH::exportFile(
						   "generator.json",
						   std::vector<unsigned char>(p->canonical.begin(), p->canonical.end())))
					   customGenerators->notice = tr("Package export is unavailable.");
				   invalidate();
			   });
		button("generator.remove." + id, tr("Remove generator"),
			   [this, id]
			   {
				   try
				   {
					   auto &s = *customGenerators;
					   s.beginSave([&](JSGen::Library &library) { library.remove(id); });
					   s.notice = tr("Saving generator library…");
				   }
				   catch (const std::exception &error)
				   {
					   customGenerators->notice = error.what();
				   }
				   invalidate();
			   });
	}
}
