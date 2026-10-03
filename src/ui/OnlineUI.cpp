// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/OnlineUI.h"

#include "AINames.h"
#include "FormatableString.h"
#include "GenerationRequest.h"
#include "InstanceConfig.h"
#include "OnlineServices.h"
#include "PlatformClient.h"
#include "GeneratorRegistry.h"

#include <ApplicationHost.h>
#include <GraphicContext.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>

namespace Glob2UI
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
}

Element onlinePanel(OnlinePanel spec, const Presentation &p)
{
	const auto &palette = frontendTheme().palette;
	std::vector<Element> heading{paragraph(spec.title, {FontRole::Heading})};
	if (!spec.subtitle.empty())
		heading.push_back(caption(spec.subtitle));
	Element titleBlock = column(std::move(heading), {p.pt(2)});
	Element header = spec.headerRight
						 ? row({expanded(titleBlock), spec.headerRight}, {p.pt(8), CrossAlign::Center})
						 : titleBlock;
	const bool phone = p.touch && p.compact();
	Element actionRow;
	if (phone && spec.thumbBlock)
		actionRow = spec.thumbBlock;
	else if (!spec.actions.empty())
		actionRow = actions(spec.actions, p, ActionStyle::Compact);
	(void)palette;
	if (p.touch)
	{
		// One-thumb sheet: title at the top, content scrolling, actions at the bottom.
		std::vector<Element> bottom;
		if (!spec.note.empty() && !phone)
			bottom.push_back(paragraph(spec.note, {FontRole::Support, true}));
		if (actionRow)
			bottom.push_back(actionRow);
		Element body = column({header, expanded(spec.body)}, {p.pt(10)});
		CardOptions options;
		options.padding = p.pt(phone ? 12 : 16);
		options.shadow = !phone;
		Element sheet = card(footer(body, column(std::move(bottom), {p.pt(6)})), options);
		if (phone)
			return sheet;
		return center(maxWidth(p.pt(1120), sheet));
	}
	std::vector<Element> footerParts;
	footerParts.push_back(expanded(paragraph(spec.note, {FontRole::Support, true})));
	if (actionRow)
		footerParts.push_back(actionRow);
	CardOptions options;
	options.padding = p.pt(16);
	Element panel = card(column({header, expanded(spec.body), divider(),
								 row(std::move(footerParts), {p.pt(12), CrossAlign::Center})},
								{p.pt(10)}),
						 options);
	const int margin = std::clamp(p.safe.w / 24, p.pt(8), p.pt(60));
	if (p.safe.w > p.pt(1240))
		return center(sized({p.pt(1180), std::min(p.safe.h - 2 * p.pt(12), p.pt(820))}, panel));
	return padding(Insets::symmetric(margin, p.pt(12)), panel);
}

Element previewPicture(GAGCore::DrawableSurface *surface, int pixels)
{
	return canvas("", {pixels, pixels},
				  [surface](Canvas &c, Rect r, const Frame &frame)
				  {
					  const auto &palette = frame.layout.theme.palette;
					  const int side = std::min(r.w, r.h);
					  const Rect box{r.x + (r.w - side) / 2, r.y + (r.h - side) / 2, side, side};
					  c.fillRect(box, palette.placeholder);
					  if (surface && surface->getW() > 0 && surface->getH() > 0)
					  {
						  const double scale =
							  std::min(double(side) / surface->getW(), double(side) / surface->getH());
						  const int w = int(surface->getW() * scale), h = int(surface->getH() * scale);
						  c.drawSurface({box.x + (side - w) / 2, box.y + (side - h) / 2, w, h}, surface);
					  }
					  c.strokeRect(box, palette.line);
				  },
				  {.keepAspect = true});
}

Element sparkline(std::vector<double> values, bool provisional, Size size)
{
	return canvas("", size,
				  [values = std::move(values), provisional](Canvas &c, Rect r, const Frame &frame)
				  {
					  if (values.size() < 2)
						  return;
					  const auto &palette = frame.layout.theme.palette;
					  const auto [low, high] = std::minmax_element(values.begin(), values.end());
					  const double span = std::max(1.0, *high - *low);
					  auto at = [&](std::size_t i)
					  {
						  const double x = r.x + (r.w - 1) * double(i) / double(values.size() - 1);
						  const double y = r.y + (r.h - 1) * (1 - (values[i] - *low) / span);
						  return Point{int(std::lround(x)), int(std::lround(y))};
					  };
					  for (std::size_t i = 1; i < values.size(); ++i)
					  {
						  if (provisional && i % 2 == 0)
							  continue;
						  const Point a = at(i - 1), b = at(i);
						  c.line(a, b, palette.ink);
						  c.line({a.x, a.y + 1}, {b.x, b.y + 1}, palette.ink);
					  }
				  });
}

Element countdownRing(std::int64_t remainingMs, std::int64_t totalMs, int pixels)
{
	return canvas("", {pixels, pixels},
				  [remainingMs, totalMs](Canvas &c, Rect r, const Frame &frame)
				  {
					  const auto &palette = frame.layout.theme.palette;
					  const int side = std::min(r.w, r.h);
					  const double cx = r.x + r.w / 2.0, cy = r.y + r.h / 2.0;
					  const double outer = side / 2.0 - 1;
					  const double fraction =
						  totalMs > 0 ? std::clamp(double(remainingMs) / double(totalMs), 0.0, 1.0) : 0;
					  const int width = std::max(3, side / 10);
					  const int steps = std::max(48, side * 2);
					  for (int ring = 0; ring < width; ++ring)
					  {
						  const double radius = outer - ring;
						  for (int i = 0; i < steps; ++i)
						  {
							  const double a0 = -kPi / 2 + 2 * kPi * i / steps;
							  const double a1 = -kPi / 2 + 2 * kPi * (i + 1) / steps;
							  const bool lit = double(i) / steps < fraction;
							  c.line({int(cx + radius * std::cos(a0)), int(cy + radius * std::sin(a0))},
									 {int(cx + radius * std::cos(a1)), int(cy + radius * std::sin(a1))},
									 lit ? palette.accent : palette.line);
						  }
					  }
					  const std::string text = std::to_string((std::max<std::int64_t>(0, remainingMs) + 999) / 1000);
					  const int tw = c.measurer().width(FontRole::Heading, text);
					  const int th = c.measurer().lineHeight(FontRole::Heading);
					  c.text({int(cx - tw / 2.0), int(cy - th / 2.0)}, FontRole::Heading, text, palette.ink);
				  });
}

Element badge(const std::string &text, GAGCore::Color color)
{
	CardOptions options;
	options.padding = 4;
	options.shadow = false;
	options.border = color;
	options.color = frontendTheme().palette.field;
	return card(label(text, {FontRole::Caption, false, TextAlign::Center, color}), options);
}

// ------------------------------------------------------------- previews

PreviewImages::PreviewImages() = default;
// scope (a member) cancels the downloads still in flight.
PreviewImages::~PreviewImages() = default;

bool PreviewImages::insert(const std::string &url, const std::string &png)
{
	auto &entry = entries[url];
	entry.pending = false;
	SDL_IOStream *stream = SDL_IOFromConstMem(png.data(), png.size());
	SDL_Surface *decoded = stream ? IMG_Load_IO(stream, true) : nullptr;
	if (!decoded)
	{
		entry.failed = true;
		return false;
	}
	entry.surface = std::make_unique<GAGCore::DrawableSurface>(decoded);
	SDL_DestroySurface(decoded);
	entry.failed = false;
	return true;
}

bool PreviewImages::insertFile(const std::string &url, const std::string &path)
{
	SDL_Surface *decoded = IMG_Load(path.c_str());
	auto &entry = entries[url];
	entry.pending = false;
	if (!decoded)
	{
		entry.failed = true;
		return false;
	}
	entry.surface = std::make_unique<GAGCore::DrawableSurface>(decoded);
	SDL_DestroySurface(decoded);
	return true;
}

Online::PlatformScope &PreviewImages::scopeFor(Online::PlatformClient &client)
{
	if (!scope || &scope->client() != &client)
		scope = std::make_unique<Online::PlatformScope>(client);
	return *scope;
}

GAGCore::DrawableSurface *PreviewImages::get(Online::PlatformClient *client, const std::string &url,
											 std::function<void()> changed)
{
	if (url.empty())
		return nullptr;
	auto found = entries.find(url);
	if (found != entries.end())
		return found->second.surface.get();
	auto &entry = entries[url];
	if (!client)
	{
		entry.failed = true;
		return nullptr;
	}
	entry.pending = true;
	scopeFor(*client).restRaw(
		HttpFetch::Method::Get, url, {}, {},
		[this, url, changed](const Online::PlatformClient::Response &response)
		{
			if (response.ok)
				insert(url, response.body);
			else
			{
				entries[url].pending = false;
				entries[url].failed = true;
			}
			if (changed)
				changed();
		},
		4 * 1024 * 1024);
	return nullptr;
}

// ----------------------------------------------------------------- text

std::string clockText(std::int64_t seconds)
{
	seconds = std::max<std::int64_t>(0, seconds);
	char buffer[32];
	if (seconds >= 3600)
		std::snprintf(buffer, sizeof buffer, "%lld:%02lld:%02lld", (long long)(seconds / 3600),
					  (long long)(seconds / 60 % 60), (long long)(seconds % 60));
	else
		std::snprintf(buffer, sizeof buffer, "%lld:%02lld", (long long)(seconds / 60), (long long)(seconds % 60));
	return buffer;
}

std::string aboutText(std::int64_t seconds)
{
	if (seconds < 45)
		return tr("[online under a minute]");
	return GAGCore::FormattableString(tr("[online about %0 min]")).arg(int(std::max<std::int64_t>(1, (seconds + 30) / 60)));
}

std::string durationText(std::int64_t seconds)
{
	seconds = std::max<std::int64_t>(0, seconds);
	if (seconds < 60)
		return GAGCore::FormattableString(tr("[online %0 s]")).arg(int(seconds));
	return GAGCore::FormattableString(tr("[results minutes %0]")).arg(int((seconds + 30) / 60));
}

std::string queueDisplayName(const std::string &id, const std::string &name)
{
	if (!name.empty())
		return name;
	std::string readable = id;
	for (char &c : readable)
		if (c == '-')
			c = ' ';
	if (!readable.empty())
		readable[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(readable[0])));
	return readable;
}

std::string pairingCode(const std::string &pairing)
{
	const auto at = pairing.find("sha256=");
	if (at == std::string::npos)
		return {};
	std::string hex;
	for (std::size_t i = at + 7; i < pairing.size() && hex.size() < 8; ++i)
		if (std::isxdigit(static_cast<unsigned char>(pairing[i])))
			hex += char(std::toupper(static_cast<unsigned char>(pairing[i])));
	return hex.size() == 8 ? hex.substr(0, 4) + " " + hex.substr(4) : std::string();
}

std::string generatorTitle(const std::string &generatorId)
{
	if (generatorId.empty())
		return {};
	try
	{
		const int method = GeneratorRegistry::builtins().idOf(generatorId);
		return tr(GenerationRequest::methodName(method));
	}
	catch (const std::exception &)
	{
	}
	std::string title = generatorId;
	bool start = true;
	for (char &c : title)
	{
		if (c == '-')
		{
			c = ' ';
			start = true;
		}
		else if (start)
		{
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			start = false;
		}
	}
	return title;
}

std::string aiTitle(const std::string &aiId)
{
	if (aiId.empty())
		return {};
	std::string title = aiId;
	title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
	return title;
}

std::string aiProfile(const std::string &aiId)
{
	const int id = AINames::parseAIName(aiId);
	return id == AINames::AI_UNKNOWN_NAME ? std::string() : AINames::getAIProfile(id);
}

std::string signedText(double value)
{
	const long rounded = std::lround(value);
	if (rounded > 0)
		return "+" + std::to_string(rounded);
	if (rounded < 0)
		return "\xE2\x88\x92" + std::to_string(-rounded); // U+2212 minus sign
	return "0";
}

std::string compactCount(std::int64_t value)
{
	if (value < 1000)
		return std::to_string(value);
	char buffer[32];
	if (value < 100000)
		std::snprintf(buffer, sizeof buffer, "%.1fk", value / 1000.0);
	else if (value < 1000000)
		std::snprintf(buffer, sizeof buffer, "%lldk", (long long)(value / 1000));
	else
		std::snprintf(buffer, sizeof buffer, "%.1fM", value / 1000000.0);
	std::string text = buffer;
	if (auto dot = text.find(".0"); dot != std::string::npos)
		text.erase(dot, 2);
	return text;
}

std::string ageText(std::int64_t at, std::int64_t now)
{
	const std::time_t then = static_cast<std::time_t>(at / 1000);
	const std::time_t current = static_cast<std::time_t>(now / 1000);
	std::tm thenTm{}, nowTm{};
#ifdef _WIN32
	localtime_s(&thenTm, &then);
	localtime_s(&nowTm, &current);
#else
	localtime_r(&then, &thenTm);
	localtime_r(&current, &nowTm);
#endif
	const std::int64_t seconds = std::max<std::int64_t>(0, (now - at) / 1000);
	if (seconds < 60)
		return tr("[online just now]");
	const bool sameDay = thenTm.tm_year == nowTm.tm_year && thenTm.tm_yday == nowTm.tm_yday;
	if (sameDay)
	{
		char buffer[16];
		std::snprintf(buffer, sizeof buffer, "%02d:%02d", thenTm.tm_hour, thenTm.tm_min);
		return GAGCore::FormattableString(tr("[online today %0]")).arg(buffer);
	}
	const std::int64_t days = seconds / 86400;
	if (days < 2 && (nowTm.tm_yday - thenTm.tm_yday + 366) % 366 == 1)
		return tr("[online yesterday]");
	if (days < 7)
	{
		static const char *names[] = {"[online sun]", "[online mon]", "[online tue]", "[online wed]",
									  "[online thu]", "[online fri]", "[online sat]"};
		return tr(names[thenTm.tm_wday]);
	}
	if (days < 30)
		return GAGCore::FormattableString(tr("[online %0 weeks ago]")).arg(int(days / 7));
	if (days < 365)
		return GAGCore::FormattableString(tr("[online %0 months ago]")).arg(int(days / 30));
	return GAGCore::FormattableString(tr("[online %0 years ago]")).arg(int(days / 365));
}

std::string originHost(const std::string &origin)
{
	const auto scheme = origin.find("://");
	return scheme == std::string::npos ? origin : origin.substr(scheme + 3);
}

std::int64_t wallClockMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::system_clock::now().time_since_epoch())
		.count();
}

Online::PlatformClient &onlineClient()
{
	auto &services = Online::services();
	if (services.client.connection() == Online::PlatformClient::Connection::Stopped)
		services.client.start(services.config.selectedOrigin());
	return services.client;
}

bool openInstancePage(const std::string &origin, const std::string &path)
{
	if (origin.empty())
		return false;
	return GAGCore::ApplicationHost::openUrl(origin + path);
}
} // namespace Glob2UI
