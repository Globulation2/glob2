// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include "HiveClient.h"
#include <set>
#include <optional>
namespace Hive
{
// A non-modal HUD stack. Only pointer events inside its bounds are captured.
class Dialog : public Glob2UI::InGameDialog
{
	std::shared_ptr<CommanderView> client;
	bool expanded = false, composerOpen = false;
	std::set<std::pair<SDL_TouchID, SDL_FingerID>> capturedFingers;
	std::set<std::pair<SDL_MouseID, Uint8>> capturedMouseButtons;
	std::optional<std::uint64_t> capturedScroll;

  public:
	std::function<void()> compose;
	bool local() const { return client->local(); }
	explicit Dialog(std::shared_ptr<CommanderView> c) : client(std::move(c)) {}
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;
	bool handle(const SDL_Event &event);
	void cancelInput();
	void setComposerOpen(bool value)
	{
		if (composerOpen != value)
		{
			composerOpen = value;
			invalidate();
		}
	}

  protected:
	double maxWidth() const override { return 300; }
	GAGGUI::ui::Rect available(const GAGGUI::ui::Presentation &,
							   const GAGGUI::ui::Metrics &) override;
	GAGGUI::ui::Rect place(GAGGUI::ui::Size measured, GAGGUI::ui::Rect area) override;
	void paintPanel(GAGGUI::ui::Canvas &, GAGGUI::ui::Rect) override {}
};
} // namespace Hive
