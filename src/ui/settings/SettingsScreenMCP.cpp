// SPDX-License-Identifier: GPL-3.0-or-later
// Settings > MCP server: immediate listener configuration and controller revocation.
#include "SettingsScreen.h"
#include "GlobalContainer.h"
#include "mcp/UIService.h"
#include <FormatableString.h>

void SettingsScreen::buildMCP()
{
#if GLOB2_DESKTOP_MCP
	auto &settings = globalContainer->settings;
	info(tr("Enabling the MCP server permits local clients to see and operate the game."));
	toggle("mcp.enabled", "Enable MCP server", "", settings.mcpEnabled,
		   [this](int value)
		   {
			   auto &settings = globalContainer->settings;
			   settings.mcpEnabled = value;
			   if (auto *service = MCP::service())
				   service->configure(settings.mcpEnabled, settings.mcpPort);
			   commit();
		   });
	number("mcp.port", tr("Port"), settings.mcpPort, 1, 65535,
		   [this](int value)
		   {
			   auto &settings = globalContainer->settings;
			   settings.mcpPort = value;
			   if (auto *service = MCP::service())
				   service->configure(settings.mcpEnabled, settings.mcpPort);
			   commit();
		   });

	auto *service = MCP::service();
	if (!service)
	{
		info(tr("MCP server unavailable in this application"));
		return;
	}
	info(service->endpoint());
	switch (service->listenerState())
	{
	case MCP::ListenerState::Disabled:
		info(tr("Disabled"));
		break;
	case MCP::ListenerState::Starting:
		info(tr("Starting"));
		break;
	case MCP::ListenerState::Running:
		info(tr("Running"));
		break;
	case MCP::ListenerState::Error:
		info(
			std::string(GAGCore::FormattableString(tr("Error: %0")).arg(mcpError(service->failureReason()))));
		break;
	}
	const auto controller = service->hasController() ? service->currentController() : tr("None");
	info(std::string(GAGCore::FormattableString(tr("Current controller: %0")).arg(controller)));
	button("mcp.copy", tr("Copy endpoint"),
		   []
		   {
			   if (auto *service = MCP::service())
				   SDL_SetClipboardText(service->endpoint().c_str());
		   });
	button("mcp.retry", tr("Retry listener"),
		   [this]
		   {
			   const auto &settings = globalContainer->settings;
			   if (auto *service = MCP::service())
				   service->configure(settings.mcpEnabled, settings.mcpPort, true);
			   invalidate();
		   });
	button("mcp.revoke", tr("Revoke control"),
		   [this]
		   {
			   if (auto *service = MCP::service())
				   service->revoke();
			   invalidate();
		   });
#endif
}
