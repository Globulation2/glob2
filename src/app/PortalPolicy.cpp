// SPDX-License-Identifier: GPL-3.0-or-later
#include "PortalPolicy.h"

#include <ApplicationHost.h>
#include <nlohmann/json.hpp>

#include <optional>

namespace PortalPolicy
{
Policy parse(std::string_view description)
{
	Policy policy;
	if (description.empty())
		return policy;
	policy.portal = true;
	policy.externalLinks = false;
	policy.onlineSignIn = false;
	const auto json = nlohmann::json::parse(description, nullptr, false);
	if (!json.is_object())
		return policy;
	auto granted = [&json](const char *key)
	{
		const auto it = json.find(key);
		return it != json.end() && it->is_boolean() && it->get<bool>();
	};
	policy.externalLinks = granted("externalLinks");
	policy.onlineSignIn = granted("onlineSignIn");
	policy.ads = granted("ads");
	if (const auto it = json.find("name"); it != json.end() && it->is_string())
		policy.name = it->get<std::string>();
	return policy;
}

namespace
{
std::optional<Policy> &installed()
{
	static std::optional<Policy> policy;
	return policy;
}
} // namespace

const Policy &current()
{
	auto &policy = installed();
	if (!policy)
		install(parse(GAGCore::ApplicationHost::portalDescription()));
	return *policy;
}

void install(const Policy &policy)
{
	installed() = policy;
	GAGCore::ApplicationHost::setExternalLinksAllowed(policy.externalLinks);
	GAGCore::ApplicationHost::setFullscreenControlAllowed(!policy.portal);
}
} // namespace PortalPolicy
