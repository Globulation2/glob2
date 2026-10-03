// SPDX-License-Identifier: GPL-3.0-or-later
#include <GameplayRecording.h>
#include <ApplicationHost.h>
#include <algorithm>
#include <sstream>
#include <typeinfo>
#include <ui/Screen.h>
#include <GraphicContext.h>
#include <GUIStyle.h>

namespace GAGGUI::ui
{
namespace
{
std::string quoted(const std::string &text)
{
	std::string out = "\"";
	for (unsigned char c : text)
	{
		if (c == '"' || c == '\\')
			out += '\\';
		if (c < 0x20)
			out += ' ';
		else
			out += char(c);
	}
	return out + "\"";
}
// Bounds are the element's own; "visible" is the part left after every
// enclosing scroll region's clip (empty when scrolled out of view).
void appendTarget(std::ostringstream &out, bool &first, const std::string &key, Rect r, Rect clip, bool enabled, const std::string &label)
{
	if (key.empty())
		return;
	const Rect v = r.intersect(clip);
	out << (first ? "" : ",") << quoted(key) << ":{\"x\":" << r.x << ",\"y\":" << r.y << ",\"w\":" << r.w << ",\"h\":" << r.h
		<< ",\"visible\":{\"x\":" << v.x << ",\"y\":" << v.y << ",\"w\":" << std::max(0, v.w) << ",\"h\":" << std::max(0, v.h) << "}"
		<< ",\"enabled\":" << (enabled ? "true" : "false") << ",\"label\":" << quoted(label) << "}";
	first = false;
}
void appendTree(std::ostringstream &out, bool &first, Node &node, Rect clip)
{
	if (node.scrollable())
		clip = clip.intersect(node.bounds);
	if (node.interactive())
	{
		appendTarget(out, first, node.key, node.bounds, clip, node.enabled(), node.accessibleText());
		for (const auto &target : node.subTargets())
			appendTarget(out, first, node.key + "/" + target.suffix, target.bounds, clip, node.enabled(), target.label);
	}
	for (auto &child : node.children)
		if (child)
			appendTree(out, first, *child, clip);
}
} // namespace

void publishControls(const void *owner, const Host &host)
{
	if (!GAGCore::ApplicationHost::controlsObserved())
		return;
	std::ostringstream out;
	out << "{\"surface\":{\"w\":" << host.presentation().viewport.w << ",\"h\":" << host.presentation().viewport.h << "},\"root\":{\"x\":"
		<< host.rootBounds().x << ",\"y\":" << host.rootBounds().y << ",\"w\":" << host.rootBounds().w << ",\"h\":" << host.rootBounds().h
		<< "},\"controls\":{";
	bool first = true;
	const Rect surface{0, 0, host.presentation().viewport.w, host.presentation().viewport.h};
	if (auto *root = host.root())
		appendTree(out, first, *root, surface);
	if (auto *popup = host.popupRoot())
		appendTree(out, first, *popup, surface);
	out << "}}";
	GAGCore::ApplicationHost::controlsChanged(owner, out.str().c_str());
}

namespace
{
bool quitRequest(const SDL_Event &event)
{
	if (event.type == SDL_EVENT_QUIT)
		return true;
	if (event.type != SDL_EVENT_KEY_DOWN)
		return false;
#ifdef USE_OSX
	if (event.key.key == SDLK_Q && (event.key.mod & SDL_KMOD_GUI))
		return true;
#endif
#ifdef USE_WIN32
	if (event.key.key == SDLK_F4 && (event.key.mod & SDL_KMOD_ALT))
		return true;
#endif
	return false;
}

void ensureMeasurer(std::unique_ptr<ToolkitTextMeasurer> &measurer, bool &touch, double &unit,
					const Theme &theme, const Presentation &p, Host &host)
{
	if (measurer && touch == p.touch && unit == p.textUnit)
		return;
	touch = p.touch;
	unit = p.textUnit;
	measurer = std::make_unique<ToolkitTextMeasurer>(theme, touch, unit);
	host.setMeasurer(measurer.get());
}
} // namespace

UIScreen::UIScreen(const Theme &theme)
	: themeValue(theme),
	  hostValue(theme, [this](const Presentation &p) { return build(p); })
{
	hostValue.setAvailable([this](const Presentation &p, const Metrics &m) { return available(p, m); });
	hostValue.setPlacement([this](Size measured, Rect area) { return place(measured, area); });
	hostValue.setEscape([this] { onEscape(); });
	hostValue.setLayoutListener([this] { publishControls(this, hostValue); });
}

UIScreen::~UIScreen() { GAGCore::ApplicationHost::controlsChanged(this, nullptr); }

Rect UIScreen::available(const Presentation &p, const Metrics &) { return p.dialog; }

void UIScreen::paintBackground(Canvas &canvas)
{
	canvas.fillRect({0, 0, canvas.size().w, canvas.size().h}, themeValue.palette.paper);
}

void UIScreen::refreshPresentation()
{
	if (!gfx)
		return;
	const auto p = resolvePresentation(*gfx, themeValue.touchTextScale);
	ensureMeasurer(measurer, measurerTouch, measurerUnit, themeValue, p, hostValue);
	hostValue.setPresentation(p);
}

void UIScreen::beginExecution(GAGCore::DrawableSurface *surface)
{
	Screen::beginExecution(surface);
	refreshPresentation();
	hostValue.invalidate();
}

void UIScreen::updateExecution(Uint32 tick)
{
	if (!run)
		return;
	const int state = int(GAGCore::Recording::recorder().status().state);
	if (state != recordingState)
	{
		recordingState = state;
		hostValue.invalidate();
	}
	lastTick = tick;
	refreshPresentation();
	Screen::updateExecution(tick);
	hostValue.update(tick);
}

void UIScreen::handleExecutionEvent(SDL_Event event)
{
	if (!run)
		return;
	GAGCore::GraphicContext::translateMouseEvent(&event);
	if (quitRequest(event))
	{
		endExecute(QUIT_APPLICATION);
		return;
	}
	if (event.type == SDL_EVENT_MOUSE_WHEEL && !scrollWheelEnabled)
		return;
	refreshPresentation();
	if (interceptEvent(event))
		return;
	hostValue.event(event);
	onEvent(event);
}

Uint32 UIScreen::executionDelay(Uint32 now, Uint32 fallback)
{
	if (!hostValue.animating())
		return fallback;
	const Uint32 elapsed = now - lastTick;
	return std::min(fallback, elapsed < 16 ? 16 - elapsed : 0);
}

void UIScreen::viewportResized(int, int, int, int)
{
	cancelExecutionInput();
	refreshPresentation();
	hostValue.relayout();
}

// The stack calls this for size and presentation changes as well as focus
// loss; the host handles focus loss itself from the window event.
void UIScreen::cancelExecutionInput() { hostValue.cancelGestures(); }

void UIScreen::paintFrame(Uint32 tick)
{
	if (!gfx)
		return;
	refreshPresentation();
	beforePaint();
	gfx->setClipRect();
	SurfaceCanvas canvas(*gfx, themeValue, hostValue.presentation());
	paintBackground(canvas);
	hostValue.paint(canvas, tick);
	afterPaint(canvas);
}

void UIScreen::drawExecution()
{
	if (!run || !gfx)
		return;
	GAGCore::Recording::recorder().screen(recordingId());
	paintFrame(lastTick);
	gfx->nextFrame();
}

UIDialog::UIDialog(const Theme &theme)
	: themeValue(theme), hostValue(theme, [this](const Presentation &p) { return build(p); })
{
	hostValue.setAvailable([this](const Presentation &p, const Metrics &m) { return available(p, m); });
	hostValue.setPlacement([this](Size measured, Rect area) { return place(measured, area); });
	hostValue.setEscape([this] { onEscape(); });
	hostValue.setLayoutListener([this] { publishControls(this, hostValue); });
}

Rect UIDialog::available(const Presentation &p, const Metrics &m)
{
	const int limit = maxWidth() < 0 ? m.dialogMaxWidth : p.pt(maxWidth());
	Rect area = p.dialog.inset(m.padding);
	const int width = std::min(area.w, limit);
	area.x += (area.w - width) / 2;
	area.w = width;
	return area;
}

Rect UIDialog::place(Size measured, Rect area)
{
	const int w = std::min(area.w, std::max(measured.w, std::min(area.w, hostValue.metrics().dialogMaxWidth / 2)));
	const int h = fillHeight() ? area.h : std::min(area.h, measured.h);
	return Rect{area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w, h};
}

UIDialog::~UIDialog() { GAGCore::ApplicationHost::controlsChanged(this, nullptr); }

void UIDialog::attach(GAGCore::DrawableSurface &target)
{
	surface = &target;
	refreshPresentation();
	hostValue.invalidate();
}

void UIDialog::refreshPresentation()
{
	if (!surface)
		return;
	const auto p = resolvePresentation(*surface, themeValue.touchTextScale);
	ensureMeasurer(measurer, measurerTouch, measurerUnit, themeValue, p, hostValue);
	hostValue.setPresentation(p);
}

bool UIDialog::event(const SDL_Event &raw)
{
	if (done)
		return false;
	SDL_Event event = raw;
	GAGCore::GraphicContext::translateMouseEvent(&event);
	return eventLogical(event);
}

bool UIDialog::eventLogical(const SDL_Event &event)
{
	if (done)
		return false;
	refreshPresentation();
	if (onEvent(event))
		return true;
	return hostValue.event(event);
}

void UIDialog::update(Uint32 tick)
{
	if (done)
		return;
	const int state = int(GAGCore::Recording::recorder().status().state);
	if (state != recordingState)
	{
		recordingState = state;
		hostValue.invalidate();
	}
	onUpdate(tick);
	refreshPresentation();
	hostValue.update(tick);
}

Rect UIDialog::panelBounds() const
{
	return hostValue.rootBounds().inset(-hostValue.metrics().padding);
}

void UIDialog::paintPanel(Canvas &canvas, Rect panel)
{
	const auto &palette = themeValue.palette;
	const int radius = hostValue.metrics().radius + 4;
	canvas.fillRounded(panel.translated(2, 3), radius, palette.shadow.applyAlpha(60));
	canvas.fillRounded(panel, radius, palette.panel.applyAlpha(248));
}

void UIDialog::draw(Uint32 tick)
{
	if (!surface || done)
		return;
	GAGCore::Recording::recorder().dialog(recordingId());
	refreshPresentation();
	hostValue.layoutIfNeeded();
	surface->setClipRect();
	SurfaceCanvas canvas(*surface, themeValue, hostValue.presentation());
	if (scrim())
		canvas.fillRect(hostValue.presentation().viewport, themeValue.palette.scrim);
	paintPanel(canvas, panelBounds());
	hostValue.paint(canvas, tick);
}

void UIDialog::cancelInput() { hostValue.cancelInput(); }

void UIDialog::finish(int result)
{
	if (done)
		return;
	done = true;
	resultValue = result;
	hostValue.endEditing();
	GAGCore::ApplicationHost::controlsChanged(this, nullptr);
}
} // namespace GAGGUI::ui
