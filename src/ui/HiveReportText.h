// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/DiagnosticText.h"
#include "hive/HiveReport.h"
namespace Glob2UI
{
inline std::string hiveReportText(const Hive::Report &report)
{
	// Remote/commander/user content is data, even when identical to a local report.
	if (report.source == Hive::ReportSource::Remote) return report.text;
	// Existing local stop acknowledgments were already translated in every locale.
	if (report.source == Hive::ReportSource::Stopped) return nativeSurfaceText("[hive stopped]", "Stopped");
	if (report.source == Hive::ReportSource::StopFailed) return nativeSurfaceText("[hive stop failed]", "Stop failed; retry the stop shortcut");
	if (!bulgarianErrorLocale()) return report.text;
	switch (report.source)
	{
	case Hive::ReportSource::StandingOrderPaused: return nativeSurfaceText("[hive standing order paused]", "A standing order needs attention and has been paused.");
	case Hive::ReportSource::ConnectionChanged: return nativeSurfaceText("[hive connection changed orders paused]", "The connection changed. Review your paused standing orders before resuming.");
	case Hive::ReportSource::FreshInstruction: return nativeSurfaceText("[hive standing order fresh instruction]", "A standing order needs a fresh instruction after reconnecting. You can cancel it or ask the commander to replace it.");
	case Hive::ReportSource::ConfirmationFailed: return nativeSurfaceText("[hive order confirmation failed]", "An order could not be confirmed. Review your standing orders before resuming.");
	case Hive::ReportSource::PreviousSending: return nativeSurfaceText("[hive previous order sending]", "Your previous order is still sending. This message has been kept; open the commander to send it again.");
	case Hive::ReportSource::OrderReceived: return literalSurfaceFormat(nativeSurfaceText("[hive order received %0]", "Order received: %0"), {report.parameter});
	case Hive::ReportSource::OrderNotConfirmed: return nativeSurfaceText("[hive order not confirmed]", "Order not confirmed. Open the commander to retry; your message has been kept.");
	case Hive::ReportSource::UpdateFailed: return nativeSurfaceText("[hive standing order update failed]", "Standing order update failed. Your order is paused locally; retry the control.");
	case Hive::ReportSource::Stopped: return nativeSurfaceText("[hive stopped]", "Stopped");
	case Hive::ReportSource::StopFailed: return nativeSurfaceText("[hive stop failed]", "Stop failed; retry the stop shortcut");
	case Hive::ReportSource::Remote: return report.text;
	}
	return report.text;
}
} // namespace Glob2UI
