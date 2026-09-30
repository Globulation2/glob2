// SPDX-License-Identifier: GPL-3.0-or-later
#include <ui/Screen.h>
#include <GraphicContext.h>
#include <GUIStyle.h>

namespace GAGGUI::ui
{
namespace
{
bool quitRequest(const SDL_Event &event)
{
	if (event.type == SDL_QUIT)
		return true;
	if (event.type != SDL_KEYDOWN)
		return false;
#ifdef USE_OSX
	if (event.key.keysym.sym == SDLK_q && (event.key.keysym.mod & KMOD_GUI))
		return true;
#endif
#ifdef USE_WIN32
	if (event.key.keysym.sym == SDLK_F4 && (event.key.keysym.mod & KMOD_ALT))
		return true;
#endif
	return false;
}

void ensureMeasurer(std::unique_ptr<ToolkitTextMeasurer> &measurer, bool &touch, double &scale,
					const Theme &theme, const Presentation &p, Host &host)
{
	if (measurer && touch == p.touch && scale == p.textScale)
		return;
	touch = p.touch;
	scale = p.textScale;
	measurer = std::make_unique<ToolkitTextMeasurer>(theme, touch, scale);
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
}

UIScreen::~UIScreen() = default;

Rect UIScreen::available(const Presentation &p, const Metrics &) { return p.dialog; }

void UIScreen::paintBackground(Canvas &canvas)
{
	canvas.fillRect({0, 0, canvas.size().w, canvas.size().h}, themeValue.palette.paper);
}

void UIScreen::refreshPresentation()
{
	if (!gfx)
		return;
	auto p = resolvePresentation(*gfx);
	p.textScale = textScale(p);
	ensureMeasurer(measurer, measurerTouch, measurerScale, themeValue, p, hostValue);
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
	if (event.type == SDL_MOUSEWHEEL && !scrollWheelEnabled)
		return;
	refreshPresentation();
	if (interceptEvent(event))
		return;
	hostValue.event(event);
	onEvent(event);
}

void UIScreen::viewportResized(int, int, int, int)
{
	cancelExecutionInput();
	refreshPresentation();
	hostValue.relayout();
}

void UIScreen::cancelExecutionInput() { hostValue.cancelInput(); }

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
	paintFrame(lastTick);
	gfx->nextFrame();
}

UIDialog::UIDialog(const Theme &theme)
	: themeValue(theme), hostValue(theme, [this](const Presentation &p) { return build(p); })
{
	hostValue.setAvailable([this](const Presentation &p, const Metrics &m) { return available(p, m); });
	hostValue.setPlacement([this](Size measured, Rect area) { return place(measured, area); });
	hostValue.setEscape([this] { onEscape(); });
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

UIDialog::~UIDialog() = default;

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
	auto p = resolvePresentation(*surface);
	p.textScale = textScale(p);
	ensureMeasurer(measurer, measurerTouch, measurerScale, themeValue, p, hostValue);
	hostValue.setPresentation(p);
}

bool UIDialog::event(const SDL_Event &raw)
{
	if (done)
		return false;
	SDL_Event event = raw;
	GAGCore::GraphicContext::translateMouseEvent(&event);
	refreshPresentation();
	if (onEvent(event))
		return true;
	return hostValue.event(event);
}

void UIDialog::update(Uint32 tick)
{
	if (done)
		return;
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
	canvas.fillRounded(panel.translated(2, 3), radius, GAGCore::Color(15, 39, 25, 60));
	canvas.fillRounded(panel, radius, palette.panel.applyAlpha(248));
}

void UIDialog::draw(Uint32 tick)
{
	if (!surface || done)
		return;
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
}
} // namespace GAGGUI::ui
