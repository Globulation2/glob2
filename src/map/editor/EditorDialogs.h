// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FertilityCalculator.h"
#include "ui/FrontendUI.h"
#include <string>
#include <vector>

class Map;

// A compact decision card over the live editor map (quit with unsaved changes,
// replace the map, share before saving, reroll the terrain look). Choices carry
// the keys choice/<index>; result() is the chosen index. Escape picks the
// cancel choice, which is always safe.
class EditorConfirmDialog : public Glob2UI::InGameDialog
{
  public:
	const char *recordingId() const override { return "editor_confirm"; }
	struct Choice
	{
		std::string label;
		// Highlighted; Return picks it. Never put this on a destructive choice.
		bool primary = false;
	};
	EditorConfirmDialog(std::string title, std::string message, std::vector<Choice> choices, int cancelChoice);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	// Semantic entry point shared with harnesses.
	void choose(int index);
	int cancelChoice() const { return cancelIndex; }
	const std::string &message() const { return text; }
	std::size_t choiceCount() const { return choices.size(); }

  protected:
	void onEscape() override { finish(cancelIndex); }
	double maxWidth() const override { return 480; }

  private:
	std::string title, text;
	std::vector<Choice> choices;
	int cancelIndex;
};

// Computes the fertility map over the editor in slices of a frame budget, so
// the map stays visible and the editor stays responsive. Cancellation leaves
// the map unchanged; completion commits the staged values before finishing.
class EditorProgressDialog : public Glob2UI::InGameDialog
{
  public:
	const char *recordingId() const override { return "editor_progress"; }
	enum
	{
		CANCELLED = 0,
		COMPLETED = 1
	};
	EditorProgressDialog(Map &map, std::string caption);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	// Advance the computation for up to budgetMs milliseconds of wall time.
	void step(unsigned budgetMs);
	void cancel() { finish(CANCELLED); }
	float progress() const { return job.progress(); }

  protected:
	void onUpdate(Uint32) override;
	void onEscape() override { cancel(); }
	double maxWidth() const override { return 420; }

  private:
	FertilityCalculator::Job job;
	std::string caption;
	int permille = 0;
	bool presented = false;
};
