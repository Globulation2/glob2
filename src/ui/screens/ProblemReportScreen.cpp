// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProblemReportScreen.h"
#include "ProblemReport.h"
#include "CrashCapture.h"
#include <ApplicationHost.h>
#include <FormatableString.h>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace Glob2UI;

struct ProblemReportScreen::Report
{
	ProblemReport::Choices choices;
	ProblemReport::SendPolicy policy = ProblemReport::currentSendPolicy();
	std::unique_ptr<ProblemReport::Upload> upload;
	std::string pendingBytes;
	std::vector<std::string> leftOut;
	// A crash kept on the device (players who may not send, or "Don't send"
	// not chosen yet) can go into the report.
	bool crashWaiting = !CrashCapture::pending(ProblemReport::userDirectory()).empty();
};

ProblemReportScreen::ProblemReportScreen() : report(std::make_unique<Report>()) {}
ProblemReportScreen::~ProblemReportScreen()
{
	// Leaving while a send is still under way: the main menu sends it later.
	if (report->upload && !report->pendingBytes.empty())
		ProblemReport::Outbox(ProblemReport::userDirectory()).add(report->pendingBytes);
}

void ProblemReportScreen::setDescription(const std::string &text)
{
	const std::string value = ProblemReport::clampUtf8(text, ProblemReport::DESCRIPTION_LIMIT);
	report->choices.description = value;
}

void ProblemReportScreen::sendReport()
{
	if (report->policy != ProblemReport::SendPolicy::Allowed || state == Status::Sending)
		return;
	const auto userDir = ProblemReport::userDirectory();
	auto built = ProblemReport::build(report->choices, ProblemReport::currentContext(userDir), userDir);
	report->leftOut = built.tooLarge;
	report->pendingBytes = std::move(built.bytes);
	report->upload = ProblemReport::send(report->pendingBytes);
	state = Status::Sending;
	invalidate();
}

void ProblemReportScreen::saveToFile()
{
	const auto userDir = ProblemReport::userDirectory();
	const auto context = ProblemReport::currentContext(userDir);
	auto built = ProblemReport::build(report->choices, context, userDir);
	report->leftOut = built.tooLarge;
	const std::string name = ProblemReport::fileName(context);
	namespace AH = GAGCore::ApplicationHost;
	bool saved = false;
	if (AH::canExportFiles())
	{
		// Browser download, Android and iOS document pickers.
		saved = AH::exportFile(name, std::vector<unsigned char>(built.bytes.begin(), built.bytes.end()));
	}
	else
	{
		// Desktop: write the bundle under reports/saved/ and offer the save
		// dialog for it. Only the newest copy is kept: older ones (and the
		// player data in them) are removed first.
		const auto dir = userDir / "reports" / "saved";
		std::error_code error;
		for (std::filesystem::directory_iterator it(dir, error), end; !error && it != end; it.increment(error))
			if (it->path().extension() == ".zip")
				std::filesystem::remove(it->path(), error);
		error.clear();
		const auto path = dir / std::filesystem::u8path(name);
		std::filesystem::create_directories(dir, error);
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		saved = out.write(built.bytes.data(), static_cast<std::streamsize>(built.bytes.size())).good();
		out.close();
		if (saved)
			AH::exportFilePath(ProblemReport::utf8(path));
	}
	state = saved ? Status::Saved : Status::SaveFailed;
	invalidate();
}

void ProblemReportScreen::onTimer(Uint32)
{
	if (!report->upload)
		return;
	const auto result = report->upload->poll();
	if (result == ProblemReport::UploadState::Pending)
		return;
	if (result == ProblemReport::UploadState::Sent)
	{
		sentId = report->upload->id();
		state = Status::Sent;
		std::clog << "Problem report sent: " << sentId << std::endl;
	}
	else if (result == ProblemReport::UploadState::Retry &&
			 ProblemReport::Outbox(ProblemReport::userDirectory()).add(report->pendingBytes))
	{
		state = Status::Queued;
		std::clog << "Problem report queued: " << report->upload->error() << std::endl;
	}
	else
	{
		state = Status::Refused;
		std::clog << "Problem report not sent: " << report->upload->error() << std::endl;
	}
	report->upload.reset();
	report->pendingBytes.clear();
	invalidate();
}

Element ProblemReportScreen::build(const Presentation &p)
{
	const bool editing = state == Status::Editing || state == Status::Refused || state == Status::SaveFailed ||
						 state == Status::Saved;
	std::vector<Element> body;
	body.push_back(paragraph(tr("[report problem explanation]")));
	TextEditorOptions text;
	text.lines = p.touch ? 4 : 5;
	body.push_back(field(tr("[What happened?]"),
						 textEditor("report.description", report->choices.description,
									[this](const std::string &value) { setDescription(value); }, text),
						 {.stacked = true}));
	ToggleOptions trailing;
	trailing.trailing = true;
	body.push_back(toggle("report.log", tr("[report recent log]"), report->choices.log,
						  [this](bool on) { report->choices.log = on; invalidate(); }, editing, trailing));
	body.push_back(toggle("report.save", tr("[report last save]"), report->choices.save,
						  [this](bool on) { report->choices.save = on; invalidate(); }, editing, trailing));
	body.push_back(toggle("report.replay", tr("[report last replay]"), report->choices.replay,
						  [this](bool on) { report->choices.replay = on; invalidate(); }, editing, trailing));
	if (report->crashWaiting)
		body.push_back(toggle("report.crash", tr("[report last crash]"), report->choices.crash,
							  [this](bool on) { report->choices.crash = on; invalidate(); }, editing, trailing));
	body.push_back(hint(tr("[report always included]")));
	switch (report->policy)
	{
	case ProblemReport::SendPolicy::AgeBand:
		body.push_back(hint(tr("[report send needs age]")));
		break;
	case ProblemReport::SendPolicy::NotBuilt:
	case ProblemReport::SendPolicy::Portal:
		body.push_back(hint(tr("[report send not built]")));
		break;
	case ProblemReport::SendPolicy::Allowed:
		break;
	}
	switch (state)
	{
	case Status::Editing:
		break;
	case Status::Sending:
		body.push_back(paragraph(tr("[report sending]")));
		break;
	case Status::Sent:
		body.push_back(paragraph(GAGCore::FormattableString(tr("[report sent %0]")).arg(sentId)));
		break;
	case Status::Queued:
		body.push_back(paragraph(tr("[report queued]")));
		break;
	case Status::Refused:
		body.push_back(paragraph(tr("[report refused]")));
		break;
	case Status::Saved:
		body.push_back(paragraph(tr("[report saved]")));
		break;
	case Status::SaveFailed:
		body.push_back(paragraph(tr("[report save failed]")));
		break;
	}
	if (!report->leftOut.empty())
	{
		std::string names;
		for (const auto &name : report->leftOut)
			names += (names.empty() ? "" : ", ") + name;
		body.push_back(hint(GAGCore::FormattableString(tr("[report too large %0]")).arg(names)));
	}

	std::vector<Element> buttons;
	ButtonOptions back;
	back.shortcut = SDLK_ESCAPE;
	back.icon = uiIcon(UIIcon::Back);
	const bool finished = state == Status::Sent || state == Status::Queued;
	buttons.push_back(expanded(button("report.back", tr("[Back]"), [this] { endExecute(0); }, back)));
	if (state != Status::Sending)
	{
		ButtonOptions save;
		save.icon = uiIcon(UIIcon::Download);
		save.primary = report->policy != ProblemReport::SendPolicy::Allowed && !finished;
		buttons.push_back(expanded(
			button("report.save-file", tr("[report save to file]"), [this] { saveToFile(); }, save)));
	}
	if (report->policy == ProblemReport::SendPolicy::Allowed && !finished)
	{
		ButtonOptions send;
		send.primary = true;
		send.icon = uiIcon(UIIcon::Send);
		send.enabled = state != Status::Sending;
		buttons.push_back(expanded(button("report.send", tr("[report send]"), [this] { sendReport(); }, send)));
	}
	return page(tr("[Report a problem]"), scroll("report.body", column(std::move(body), {p.pt(8)})),
				row(std::move(buttons), {p.pt(8)}), p, 560);
}
