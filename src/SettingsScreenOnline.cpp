// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
// Settings > Online (multiplayer mock-up 8): which server to play on (the official
// one, a remembered one or any address, checked before switching), the display name,
// the sign-in methods linked to the account on that server, and sign-out.
#include "InstanceConfig.h"
#include "MapCatalog.h"
#include "OnlineServices.h"
#include "PlatformClient.h"
#include "SettingsScreen.h"
#include "GlobalContainer.h"
#include "SimVersion.h"
#include <ApplicationHost.h>
#include <FormatableString.h>

using namespace Glob2UI;
using Online::Json;

struct SettingsScreen::OnlineState
{
	std::string origin; // the client's instance when the category was built
	Json info;          // GET /api/v1/instance of that instance
	bool fetchingInfo = false;
	std::string serverDraft, nameDraft;
	bool nameEdited = false;
	// Check of another server: its description or why it failed.
	std::unique_ptr<HttpFetch::Fetch> check;
	std::string checkedOrigin, checkProblem;
	Json checked;
	std::string notice;
	// The category's platform calls and listener. The screen is the only
	// owner of this state, so closing it cancels them: handlers capture the
	// screen, never a copy of the state.
	std::unique_ptr<Online::PlatformScope> calls;
};

namespace
{
std::string hostOf(const std::string &origin)
{
	const auto scheme = origin.find("://");
	return scheme == std::string::npos ? origin : origin.substr(scheme + 3);
}

bool supportsThisBuild(const Json &info)
{
	const auto local = Online::SimVersion::local();
	for (const auto &version : info.value("supportedSimVersions", Json::array()))
		if (version.value("versionMinor", -1) == int(local.versionMinor) && version.value("netProtocol", -1) == int(local.netProtocol) &&
			version.value("dataHash", "") == local.dataHash)
			return true;
	return false;
}

std::string providerLabel(const Json &provider)
{
	return provider.value("displayName", provider.value("id", ""));
}
} // namespace

void SettingsScreen::pollOnline()
{
	if (!online)
		return;
	if (online->check)
	{
		const auto state = online->check->state();
		if (state != HttpFetch::State::Pending)
		{
			online->checked = Json();
			online->checkProblem.clear();
			if (state == HttpFetch::State::Done && online->check->response().status == 200)
			{
				online->checked = Json::parse(online->check->response().body, nullptr, false);
				if (!online->checked.is_object())
					online->checkProblem = tr("Not a Globulation 2 server.");
			}
			else
				online->checkProblem = tr("Could not reach this server.");
			online->check.reset();
			invalidate();
		}
	}
}

void SettingsScreen::unlinkProvider(const std::string &provider, const std::string &name)
{
	if (!online || !online->calls)
		return;
	online->calls->rest(HttpFetch::Method::Delete, Online::Api::accountIdentity(provider), Json(),
						[this, name](const Online::PlatformClient::Response &r) {
							online->notice = r.ok ? std::string(GAGCore::FormattableString(tr("%0 is no longer linked.")).arg(name))
												 : r.error.message;
							invalidate();
							if (r.ok)
								online->calls->refreshAccount([this](const Online::PlatformClient::Response &) { invalidate(); });
						});
}

void SettingsScreen::buildHiveMind()
{
	auto &config = Online::services().config;
	const auto &client = Online::services().client;
	const std::string origin = client.origin().empty() ? config.selectedOrigin() : client.origin();
	toggle("hive.enabled", "Enable commander shortcuts", "Command your colony without opening a menu.", globalContainer->settings.hiveMindEnabled,
		[this](int v){globalContainer->settings.hiveMindEnabled=v;commit();});
	toggle("hive.supervision", "Allow supervision for new commands", "The commander may follow up on standing orders. Follow-ups use credits; installed orders keep running without credits.", globalContainer->settings.hiveMindSupervision,
		[this](int v){globalContainer->settings.hiveMindSupervision=v;commit();});
	info(tr("Default shortcuts: Ctrl+Enter to give an order; Ctrl+Shift+Enter to stop commander work. Change them under Controls. Standing-order cards have separate pause and cancel controls."));
	button("hive.account", tr("Commander account and credits"), [origin]{GAGCore::ApplicationHost::openUrl(origin + "/commander");});
	info(tr("Manage credits and purchases in your browser. Commander assistance is permitted in ranked play."));
}

void SettingsScreen::buildOnline()
{
	auto &services = Online::services();
	auto &client = services.client;
	auto &config = services.config;
	if (!online)
	{
		online = std::make_shared<OnlineState>();
		online->calls = std::make_unique<Online::PlatformScope>(client);
		online->calls->onStateChange([this] { invalidate(); });
	}
	const std::string origin = client.origin().empty() ? config.selectedOrigin() : client.origin();
	if (online->origin != origin)
	{
		online->origin = origin;
		online->info = Json();
		online->fetchingInfo = false;
		online->nameEdited = false;
	}
	if (online->info.is_null() && !online->fetchingInfo)
	{
		online->fetchingInfo = true;
		online->calls->instanceInfo([this](const Online::PlatformClient::Response &r) {
			online->fetchingInfo = false;
			if (r.ok)
			{
				online->info = r.result;
				invalidate();
			}
		});
	}
	const auto &account = client.account();
	if (!online->nameEdited)
		online->nameDraft = account ? account->displayName : std::string();
	const bool connected = client.connection() == Online::PlatformClient::Connection::Online;
	const bool stopped = client.connection() == Online::PlatformClient::Connection::Stopped;

	info(tr("Choose which server you play on and how you sign in to it."));
	section("Server");
	auto serverRow = [this, origin, connected, stopped](const std::string &server, const std::string &title, const std::string &detail, bool forgettable) {
		const bool current = server == origin;
		const std::string key = "online.select." + hostOf(server);
		custom(key, [this, server, title, detail, current, forgettable, connected, stopped](const Presentation &p) {
			ButtonOptions select;
			select.flat = true;
			select.alignLeft = true;
			select.selected = current;
			std::vector<Element> cells{expanded(Glob2UI::button("online.select." + hostOf(server), title, [this, server] {
											auto &config = Online::services().config;
											if (config.selectInstance(server))
											{
												config.save();
												Online::services().client.start(config.selectedOrigin());
											}
											invalidate();
										}, select))};
			if (current)
				cells.push_back(caption(connected ? tr("connected") : stopped ? tr("not connected") : tr("connecting…"), false));
			if (forgettable && !current)
				cells.push_back(Glob2UI::button("online.forget." + hostOf(server), tr("Forget"), [this, server] {
					auto &config = Online::services().config;
					config.forget(server);
					config.save();
					invalidate();
				}));
			return column({row(std::move(cells), {p.pt(8), CrossAlign::Center}), padding({p.pt(12), 0, 0, 0}, caption(detail))}, {p.pt(2)});
		});
	};
	serverRow(Online::OFFICIAL_INSTANCE_ORIGIN, hostOf(Online::OFFICIAL_INSTANCE_ORIGIN), tr("The official Globulation 2 server · ranked queues, leaderboards, map catalog"), false);
	for (const auto &[server, record] : config.instances())
	{
		// A former official origin keeps its record (credentials stay keyed by
		// the origin that issued them) but is the official server's row.
		if (server == Online::OFFICIAL_INSTANCE_ORIGIN || Online::isFormerOfficialOrigin(server))
			continue;
		std::string detail = tr("used before");
		if (!record.lastDisplayName.empty())
			detail += " · " + std::string(GAGCore::FormattableString(tr("you were %0 there")).arg(record.lastDisplayName));
		if (record.trusted)
			detail += " · " + tr("trusted");
		serverRow(server, hostOf(server), detail, true);
	}
	custom("online.other.field", [this](const Presentation &p) {
		auto check = [this] {
			const auto normalized = Online::normalizeOrigin(online->serverDraft);
			online->checked = Json();
			online->checkProblem.clear();
			if (!normalized)
			{
				online->checkProblem = tr("Enter an https:// address.");
				invalidate();
				return;
			}
			online->checkedOrigin = Online::currentOrigin(*normalized);
			online->check = HttpFetch::start({HttpFetch::Method::Get, Online::apiUrl(online->checkedOrigin, Online::Api::instance())});
			invalidate();
		};
		TextFieldOptions options;
		options.placeholder = "https://play.example.org";
		options.submit = [check](const std::string &) { check(); };
		auto edit = textField("online.other.field", online->serverDraft, [this](const std::string &v) { online->serverDraft = v; }, options);
		return field(tr("Another server"), row({expanded(edit), Glob2UI::button("online.check", tr("Check"), check)}, {p.pt(6), CrossAlign::Center}),
					 {tr("Run by a community, a club or yourself. Paste its address."), p.touch ? 220.0 : 320.0});
	});
	// The inline result of Check, under the row.
	if (online->check)
		info(tr("Checking…"));
	else if (!online->checkProblem.empty())
		info(online->checkProblem);
	else if (online->checked.is_object())
	{
		const bool compatible = supportsThisBuild(online->checked);
		std::string queues;
		for (const auto &queue : online->checked.value("queues", Json::array()))
			queues += (queues.empty() ? "" : ", ") + queue.value("name", "");
		std::string methods = online->checked.value("guestsAllowed", false) ? tr("guest") : "";
		for (const auto &provider : online->checked.value("authProviders", Json::array()))
			methods += (methods.empty() ? "" : ", ") + providerLabel(provider);
		const std::string name = online->checked.value("name", hostOf(online->checkedOrigin));
		const std::string target = online->checkedOrigin;
		custom(compatible ? "online.switch" : "", [this, compatible, queues, methods, name, target](const Presentation &p) {
			std::vector<Element> lines{row({label("\"" + name + "\""), caption(compatible ? tr("compatible with this version") : tr("plays another version"), false)}, {p.pt(8), CrossAlign::Center}),
									   paragraph(GAGCore::FormattableString(tr("Sign in with: %0 · queues: %1")).arg(methods.empty() ? "–" : methods).arg(queues.empty() ? "–" : queues), {FontRole::Support, true})};
			if (compatible)
				lines.push_back(Glob2UI::button("online.switch", tr("Switch to this server"), [this, target] {
					auto &services = Online::services();
					services.config.trust(target, true);
					services.config.selectInstance(target);
					services.config.save();
					services.client.start(services.config.selectedOrigin());
					online->checked = Json();
					online->serverDraft.clear();
					invalidate();
				}, {.primary = true}));
			return column(std::move(lines), {p.pt(4)});
		});
	}

	add("", Kind::Section, GAGCore::FormattableString(tr("Your account on %0")).arg(hostOf(origin)));
	if (!account)
		info(connected ? tr("Not signed in.") : stopped ? tr("Open Play online to connect.") : tr("Connecting…"));
	else
	{
		const bool canRename = account->raw.value("canRename", account->kind == "registered");
		custom("online.name.field", [this, canRename](const Presentation &p) {
			TextFieldOptions options;
			options.maxLength = 32;
			options.enabled = canRename;
			auto edit = textField("online.name.field", online->nameDraft, [this](const std::string &v) {
				online->nameDraft = v;
				online->nameEdited = true;
			}, options);
			auto save = Glob2UI::button("online.name.save", tr("Save"), [this] {
				online->calls->rename(online->nameDraft, [this](const Online::PlatformClient::Response &r) {
					online->notice = r.ok ? tr("Display name saved.") : r.error.message;
					online->nameEdited = false;
					invalidate();
				});
			}, {.enabled = canRename});
			return field(tr("Display name"), row({expanded(edit), save}, {p.pt(6), CrossAlign::Center}),
						 {canRename ? tr("Shown in rooms, matches and the leaderboard. Change once every 30 days.") : tr("Guests keep a generated name; sign in to choose one."), p.touch ? 220.0 : 300.0});
		});
		if (!online->notice.empty())
			info(online->notice);
		// Sign-in methods: every provider the server offers, linked or not.
		Json linked = account->raw.value("identities", Json::array());
		Json providers = online->info.is_object() ? online->info.value("authProviders", Json::array()) : Json::array();
		for (const auto &provider : providers)
		{
			if (provider.value("kind", "") == "local")
				continue;
			const std::string id = provider.value("id", "");
			std::string detail = tr("Not linked");
			bool isLinked = false;
			for (const auto &identity : linked)
				if (identity.value("provider", "") == id)
				{
					isLinked = true;
					detail = identity.value("email", tr("linked"));
				}
			const std::string name = providerLabel(provider);
			custom("online.link." + id, [this, id, name, detail, isLinked, last = linked.size() <= 1](const Presentation &p) {
				ButtonOptions action;
				// The server refuses to remove an account's last way to sign in, so the
				// button is off for it (DELETE /api/v1/accounts/me/identities/{provider}).
				action.enabled = !isLinked || !last;
				if (isLinked && last)
					action.tooltip = tr("The last sign-in method can't be unlinked.");
				return row({expanded(column({label(name), caption(detail)}, {0})),
							Glob2UI::button("online.link." + id, isLinked ? tr("Unlink") : tr("Link"), [this, id, name, isLinked] {
								if (isLinked)
									unlinkProvider(id, name);
								else
									Online::services().client.beginBrowserSignIn("link", id);
							}, action)},
						   {p.pt(8), CrossAlign::Center});
			});
		}
		const auto &handoff = client.handoff();
		if (handoff.state == Online::PlatformClient::Handoff::State::Waiting)
			info(GAGCore::FormattableString(tr("Finish in your browser. It shows the code %0.")).arg(handoff.confirmationCode));
		if (account->kind == "registered")
			custom("online.signout.button", [this](const Presentation &p) {
				return row({expanded(column({label(tr("Sign out on this device")), paragraph(tr("You'll be a new guest until you sign in again. Your account and matches stay on the server."), {FontRole::Support, true})}, {0})),
							Glob2UI::button("online.signout.button", tr("Sign out"), [this] { Online::services().client.signOut(); invalidate(); })},
						   {p.pt(8), CrossAlign::Center});
			});
		custom("online.data.open", [this, origin](const Presentation &p) {
			return row({expanded(column({label(tr("Download or delete my data")), caption(GAGCore::FormattableString(tr("Opens your account page on %0.")).arg(hostOf(origin)))}, {0})),
						Glob2UI::button("online.data.open", tr("Open"), [origin] { GAGCore::ApplicationHost::openUrl(origin + "/account"); })},
					   {p.pt(8), CrossAlign::Center});
		});
	}
}
