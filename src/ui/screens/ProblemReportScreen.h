// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/FrontendUI.h"
#include <memory>
#include <string>

// Report a Problem (docs/multiplayer/problem-reports.md): "What happened?",
// which files to attach, then Send (players 13 or over, in builds that can
// upload) or Save to file (everyone). A report that cannot reach the server
// waits in the outbox and goes out from the main menu later
// (src/app/ProblemReport.h).
class ProblemReportScreen : public Glob2UI::Screen
{
  public:
	const char *recordingId() const override { return "problem_report"; }
	ProblemReportScreen();
	~ProblemReportScreen() override;
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;
	void onTimer(Uint32 tick) override;

	enum class Status
	{
		Editing,
		Sending,
		Sent,
		Queued,
		Refused,
		Saved,
		SaveFailed,
	};
	// For tests and the MCP UI service.
	Status status() const { return state; }
	const std::string &reportId() const { return sentId; }
	void setDescription(const std::string &text);
	void sendReport();
	void saveToFile();

  protected:
	void onEscape() override { endExecute(0); }

  private:
	struct Report; // ProblemReport's choices, policy and upload
	std::unique_ptr<Report> report;
	Status state = Status::Editing;
	std::string sentId;
};
