// SPDX-License-Identifier: GPL-3.0-or-later
#include "DiscordCommunity.h"
#include <ApplicationHost.h>

namespace Glob2UI
{
namespace
{
// One invitation shared by every community entry point.
constexpr const char *inviteUrl = "https://discord.gg/UyeqP55KpV";
} // namespace

Element DiscordCommunity::build(const Presentation &p, std::function<void()> changed)
{
	TextFieldOptions link;
	link.selectForCopy = true;
	std::vector<Element> body{paragraph(tr("[discord community intro]")),
							  hint(tr("[discord opens externally]")),
							  textField("discord/link", inviteUrl, {}, link)};
	if (!status.empty())
		body.push_back(paragraph(status));
	MenuAction join{"discord/join", tr("[Join Discord]"),
					[this, changed]
					{
						status = tr(GAGCore::ApplicationHost::openUrl(inviteUrl)
										? "[discord opened]"
										: "[discord open failed]");
						changed();
					},
					true, SDLK_RETURN};
	MenuAction copy{"discord/copy", tr("[Copy link]"), [this, changed]
					{
						status = tr(GAGCore::ApplicationHost::copyText(inviteUrl)
										? "[discord link copied]"
										: "[discord copy failed]");
						changed();
					}};
	MenuAction back{"discord/back", tr("[Back]"),
					[this, changed]
					{
						visible = false;
						changed();
					},
					false, SDLK_ESCAPE};
	join.icon = uiIcon(UIIcon::ExternalLink);
	copy.icon = uiIcon(UIIcon::Copy);
	return page(tr("[Globulation 2 Discord]"),
				scroll("discord/body", column(std::move(body), {p.pt(12)})),
				actions({std::move(join), std::move(copy), std::move(back)}, p), p, 520);
}
} // namespace Glob2UI
