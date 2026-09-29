// SPDX-License-Identifier: GPL-3.0-or-later
#include <FormatableString.h>
#include <Toolkit.h>
#pragma once

#include "EditorTouchWidgets.h"
#include "GameGUILoadSave.h"
#include "GlobalContainer.h"
#include "MobileSafeArea.h"
#include <GUITextInput.h>
#include <TouchInput.h>
#include <StringTable.h>

// A presentation of the existing file-dialog model, owned by PhoneEditor while
// its LoadSaveScreen is alive. Persistence, filename conversion and directory
// traversal remain in LoadSaveScreen. No desktop widget geometry is consulted.
class EditorFileView
{
	friend class GameGUITouchHarness;
	using Rect = GAGCore::ViewRect;
	using Point = GAGCore::ViewPoint;
	LoadSaveScreen &screen;
	GAGCore::TouchInput pointer;
	LoadSaveScreen::FilePresentation model;
	Rect bounds, files, filename, cancelButton, primaryButton, exportButton, statusBounds;
	double unit = 1, offset = 0, maximum = 0;
	int pressed = -1, focusedAction = 0;
	bool scrolling = false, editing = false;
	std::string preedit;
	std::vector<std::string> statusLines;
	static constexpr int Name = 1, Cancel = 2, Primary = 3, Export = 4, FirstFile = 100;

	static std::string tr(const char *key)
	{
		return GAGCore::Toolkit::getStringTable()->getString(key);
	}
	void prepare(const GAGCore::ViewRect *testViewport = nullptr)
	{
		auto *gfx = globalContainer->gfx;
		model = screen.filePresentation();
		unit = gfx->logicalUnitsPerPoint();
		const auto safe = testViewport ? *testViewport : GAGCore::mobileDialogSafe(gfx);
		const bool shortView = safe.h < 240 * unit;
		const bool minimal = !model.load && safe.h < 180 * unit;
		const double pad = (minimal     ? 4
							: shortView ? 8
										: 12) *
						   unit,
					 gap = (shortView ? 4 : 8) * unit;
		const double width = std::max(1., std::min(safe.w - 16 * unit, 560 * unit));
		const double titleHeight = (minimal ? 0 : shortView ? 28 : 32) * unit;
		const double nameHeight = model.load ? 0 : (shortView ? 44 : 64) * unit;
		statusLines = GAGCore::wrapTouchText(GAGCore::Toolkit::getFont("standard"), model.status,
											 (width - 2 * pad) / unit);
		const int lines = std::min(shortView ? 1 : 3, int(statusLines.size()));
		const double statusHeight = lines ? lines * 20 * unit + gap : 0;
		const double wanted = 2 * pad + titleHeight + gap + nameHeight + statusHeight + 24 * unit +
							  std::clamp(int(model.files.size()), 1, 6) * 44 * unit + gap +
							  48 * unit;
		const double height = std::max(1., std::min(safe.h - 16 * unit, wanted));
		bounds = {safe.x + (safe.w - width) / 2, safe.y + (safe.h - height) / 2, width, height};
		const double x = bounds.x + pad, inner = width - 2 * pad;
		const double footer = bounds.y + height - pad - 44 * unit;
		const int count = model.canExport ? 3 : 2;
		const double buttonWidth = (inner - (count - 1) * gap) / count;
		cancelButton = {x, footer, buttonWidth, 44 * unit};
		primaryButton = {x + inner - buttonWidth, footer, buttonWidth, 44 * unit};
		exportButton =
			model.canExport ? Rect{x + buttonWidth + gap, footer, buttonWidth, 44 * unit} : Rect{};
		double row = bounds.y + pad + titleHeight + (titleHeight ? gap : 0);
		filename = {};
		if (!model.load)
		{
			filename = {x, row + (shortView ? 0 : 20 * unit), inner, 44 * unit};
			row += nameHeight + gap;
		}
		statusBounds = {x, row, inner, statusHeight};
		row += statusHeight;
		const double room = std::max(0., footer - gap - row);
		// Keyboard-short layouts retain the filename and actions. The file list
		// returns automatically when there is space; draft text is never rebuilt.
		files = {x, row + (room >= 68 * unit ? 24 * unit : 0), inner,
				 room >= 44 * unit ? room - (room >= 68 * unit ? 24 * unit : 0) : 0};
		maximum = std::max(0., model.files.size() * 44 * unit - files.h);
		offset = std::clamp(offset, 0., maximum);
		// With a keyboard, retain only the draft and actions; list/status return
		// when the keyboard is dismissed instead of overlapping the footer.
		if (minimal)
		{
			files = {};
			statusBounds = {};
			statusLines.clear();
		}
		if (minimal && safe.h < 112 * unit)
		{
			const double action = std::min(88 * unit, inner / 4), h = std::min(44 * unit, height);
			const double y = bounds.y + (height - h) / 2;
			filename = {x, y, std::max(1., inner - 2 * action - 2 * gap), h};
			cancelButton = {filename.x + filename.w + gap, y, action, h};
			primaryButton = {cancelButton.x + action + gap, y, action, h};
			files = {};
			statusBounds = {};
			statusLines.clear();
			exportButton = {};
		}
	}
	int hit(Point point) const
	{
		if (cancelButton.contains(point))
			return Cancel;
		if (primaryButton.contains(point))
			return Primary;
		if (model.canExport && exportButton.contains(point))
			return Export;
		if (!model.load && filename.contains(point))
			return Name;
		if (files.h > 0 && files.contains(point))
		{
			const int index = int((point.y - files.y + offset) / (44 * unit));
			if (index >= 0 && index < int(model.files.size()))
				return FirstFile + index;
		}
		return -1;
	}
	void activate(int target)
	{
		if (model.busy || target < 0)
			return;
		focusedAction = target;
		if (target == Name)
		{
			auto *input = screen.nameInput();
			input->activate();
			input->setCursorPos(input->getText().size());
			editing = true;
			SDL_StartTextInput();
			return;
		}
		if (target >= FirstFile)
		{
			preedit.clear();
			if (editing)
				SDL_StopTextInput();
			editing = false;
			screen.nameInput()->deactivate();
			focusedAction = Primary;
			screen.selectPresentedFile(target - FirstFile);
			return;
		}
		if (target == Cancel)
		{
			screen.cancelPresentedFile();
			return;
		}
		if (target == Export)
		{
			screen.exportPresentedFile();
			return;
		}
		if (target == Primary && preedit.empty())
			screen.confirmPresentedFile();
	}
	void actions(const std::vector<GAGCore::TouchAction> &events)
	{
		for (const auto &action : events)
		{
			if (action.kind == GAGCore::TouchActionKind::Pan && scrolling)
				offset = std::clamp(offset - action.point.y, 0., maximum);
			else if (action.kind == GAGCore::TouchActionKind::Select &&
					 hit(action.point) == pressed)
				activate(pressed);
			else if (action.kind == GAGCore::TouchActionKind::Cancel)
				pressed = -1;
		}
	}
	void button(Rect r, const std::string &caption, bool enabled, bool primary = false)
	{
		auto *gfx = globalContainer->gfx;
		EditorTouch::button(gfx, r, caption, primary && enabled);
		if (!enabled)
			gfx->drawFilledRect(int(r.x), int(r.y), int(r.w), int(r.h),
								GAGCore::Color(25, 20, 35, 100));
	}
	void drawName()
	{
		auto *gfx = globalContainer->gfx;
		auto *input = screen.nameInput();
		if (filename.h <= 0)
			return;
		if (filename.y - statusBounds.y < -48 * unit)
			EditorTouch::label(gfx, {filename.x, filename.y - 20 * unit, filename.w, 20 * unit},
							   GAGCore::Toolkit::getStringTable()->getString("[File name]"));
		EditorTouch::button(gfx, filename, "");
		auto *font = GAGCore::Toolkit::getFont("standard");
		InGameTouchTheme::TextStyle ink(font);
		const auto text = input->displayPreedit(preedit);
		size_t cursor = std::min(input->cursorPosition(), input->getText().size());
		if (!preedit.empty())
			cursor += preedit.size();
		size_t start = 0;
		const int available = std::max(1, int(filename.w / unit) - 16);
		// Keep the native UTF-8 cursor visible for long names without changing
		// TextInput's committed text or its composition/cursor model.
		while (start < cursor &&
			   font->getStringWidth(text.substr(start, cursor - start)) > available - 8)
		{
			++start;
			while (start < cursor && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80)
				++start;
		}
		SDL_Rect clip{int(filename.x), int(filename.y), int(filename.w), int(filename.h)};
		gfx->setUITransform(unit, filename.x + 8 * unit,
							filename.y + (filename.h - font->getStringHeight("Ag") * unit) / 2,
							&clip);
		gfx->drawString(0, 0, font, text.substr(start), available);
		if (editing && (SDL_GetTicks() / 500) % 2 == 0)
		{
			const int caret = font->getStringWidth(text.substr(start, cursor - start));
			gfx->drawLine(caret, 0, caret, font->getStringHeight("Ag"), InGameTouchTheme::ink);
		}
		gfx->setUITransform();
		gfx->setClipRect();
		if (!model.busy)
			input->presentBrowserInput(clip, gfx->getW(), gfx->getH());
		if (editing)
			SDL_SetTextInputRect(&clip);
	}

  public:
	explicit EditorFileView(LoadSaveScreen &owner) : screen(owner)
	{
		screen.nameInput()->deactivate();
		focusedAction = screen.filePresentation().load ? Primary : Name;
	}
	~EditorFileView()
	{
		if (editing)
			SDL_StopTextInput();
	}
	// Cancellation stops gestures/preedit, retaining the committed filename.
	// The owner resets this view before replacing/destroying its dialog.
	void cancel()
	{
		pointer.cancel();
		pressed = -1;
		scrolling = false;
		preedit.clear();
	}
	void draw()
	{
		prepare();
		if (model.busy && editing)
		{
			editing = false;
			screen.nameInput()->deactivate();
			SDL_StopTextInput();
		}
		auto *gfx = globalContainer->gfx;
		EditorTouch::panel(gfx, bounds);
		const double pad = (GAGCore::mobileDialogSafe(gfx).h < 240 * unit ? 8 : 12) * unit;
		if (model.load || filename.y - bounds.y >= 28 * unit)
			EditorTouch::label(
				gfx, {bounds.x + pad, bounds.y + pad, bounds.w - 2 * pad, 32 * unit},
				model.title.empty()
					? (model.load ? GAGCore::Toolkit::getStringTable()->getString("[Load map]")
								  : GAGCore::Toolkit::getStringTable()->getString("[Save map]"))
					: model.title);
		drawName();
		for (size_t i = 0; i < statusLines.size() && (i + 1) * 20 * unit <= statusBounds.h; ++i)
			EditorTouch::label(
				gfx, {statusBounds.x, statusBounds.y + i * 20 * unit, statusBounds.w, 20 * unit},
				statusLines[i]);
		if (files.y >= statusBounds.y + statusBounds.h + 20 * unit)
			EditorTouch::label(gfx, {files.x, files.y - 24 * unit, files.w, 24 * unit},
							   GAGCore::FormattableString(
								   GAGCore::Toolkit::getStringTable()->getString("[Files · %0]"))
								   .arg(model.files.size()));
		if (files.h > 0)
		{
			if (model.files.empty())
				EditorTouch::label(
					gfx, files,
					model.load
						? GAGCore::Toolkit::getStringTable()->getString("[No files in this folder]")
						: GAGCore::Toolkit::getStringTable()->getString("[No files saved yet]"));
			for (size_t i = 0; i < model.files.size(); ++i)
			{
				Rect row{files.x, files.y + i * 44 * unit - offset, files.w - 8 * unit, 44 * unit};
				if (row.y < files.y || row.y + row.h > files.y + files.h)
					continue;
				button(row, model.files[i], !model.busy, int(i) == model.selected);
			}
			if (maximum > 0)
			{
				const double thumb = std::max(20 * unit, files.h * files.h / (files.h + maximum));
				gfx->drawFilledRect(int(files.x + files.w - 4 * unit),
									int(files.y + (files.h - thumb) * offset / maximum),
									std::max(1, int(3 * unit)), int(thumb),
									InGameTouchTheme::border);
			}
		}
		button(cancelButton, tr("[Cancel]"), !model.busy);
		if (model.canExport && exportButton.w > 0)
			button(exportButton, GAGCore::Toolkit::getStringTable()->getString("[Export]"),
				   !model.busy);
		const bool ready = !model.busy && !model.name.empty() && preedit.empty();
		button(primaryButton,
			   model.busy     ? GAGCore::Toolkit::getStringTable()->getString("[settings Saving…]")
			   : model.failed ? GAGCore::Toolkit::getStringTable()->getString("[settings Retry]")
			   : model.load   ? GAGCore::Toolkit::getStringTable()->getString("[load]")
							  : GAGCore::Toolkit::getStringTable()->getString("[Save]"),
			   ready, true);
		const Rect focused = focusedAction == Cancel    ? cancelButton
							 : focusedAction == Export  ? exportButton
							 : focusedAction == Primary ? primaryButton
														: filename;
		if (!model.busy && focused.w > 0)
			gfx->drawRect(int(focused.x + 2 * unit), int(focused.y + 2 * unit),
						  int(focused.w - 4 * unit), int(focused.h - 4 * unit),
						  InGameTouchTheme::border);
	}
	bool event(SDL_Event event)
	{
		prepare();
		if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
											  event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
											  event.window.event == SDL_WINDOWEVENT_RESIZED))
		{
			cancel();
			return true;
		}
		if (event.type == SDL_TEXTEDITING)
		{
			if (editing)
				preedit = event.edit.text;
			return true;
		}
		if (event.type == SDL_TEXTINPUT)
		{
			if (editing && !model.busy)
			{
				preedit.clear();
				screen.editName(event);
			}
			return true;
		}
		if (event.type == SDL_KEYDOWN)
		{
			if (event.key.keysym.sym == SDLK_ESCAPE)
			{
				if (!preedit.empty())
					preedit.clear();
				else
					activate(Cancel);
				return true;
			}
			if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER)
			{
				if (preedit.empty())
					activate(editing || focusedAction == Name ? Primary : focusedAction);
				return true;
			}
			if (event.key.keysym.sym == SDLK_TAB)
			{
				std::vector<int> order;
				if (!model.load)
					order.push_back(Name);
				order.push_back(Cancel);
				if (model.canExport)
					order.push_back(Export);
				order.push_back(Primary);
				auto found = std::find(order.begin(), order.end(), focusedAction);
				int index = found == order.end() ? 0 : int(found - order.begin());
				const int delta = (event.key.keysym.mod & KMOD_SHIFT) ? -1 : 1;
				focusedAction = order[(index + delta + int(order.size())) % int(order.size())];
				if (focusedAction == Name)
					activate(Name);
				else if (editing)
				{
					editing = false;
					screen.nameInput()->deactivate();
					SDL_StopTextInput();
				}
				return true;
			}
			if (editing)
				screen.editName(event);
			else if (event.key.keysym.sym == SDLK_UP || event.key.keysym.sym == SDLK_DOWN)
			{
				const int selected =
					std::clamp(model.selected + (event.key.keysym.sym == SDLK_UP ? -1 : 1), 0,
							   std::max(0, int(model.files.size()) - 1));
				screen.selectPresentedFile(selected);
				offset = std::clamp(selected * 44 * unit, 0., maximum);
			}
			return true;
		}
		if (event.type == SDL_MOUSEWHEEL)
		{
			offset =
				std::clamp(offset - event.wheel.y * 44 * unit *
										(event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1),
						   0., maximum);
			return true;
		}
		Point point;
		int phase = -1;
		std::int64_t device = -1, finger = 0;
		if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION ||
			event.type == SDL_FINGERUP)
		{
			point = {event.tfinger.x * globalContainer->gfx->getW(),
					 event.tfinger.y * globalContainer->gfx->getH()};
			device = event.tfinger.touchId;
			finger = event.tfinger.fingerId;
			phase = event.type == SDL_FINGERDOWN ? 0 : event.type == SDL_FINGERMOTION ? 1 : 2;
		}
		else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP)
		{
			if (event.button.which == SDL_TOUCH_MOUSEID || event.button.button != SDL_BUTTON_LEFT)
				return true;
			point = {double(event.button.x), double(event.button.y)};
			phase = event.type == SDL_MOUSEBUTTONDOWN ? 0 : 2;
		}
		else if (event.type == SDL_MOUSEMOTION && event.motion.which != SDL_TOUCH_MOUSEID)
		{
			point = {double(event.motion.x), double(event.motion.y)};
			phase = 1;
		}
		if (phase == 0)
		{
			if (!pointer.hasPointers())
			{
				pressed = hit(point);
				scrolling = files.h > 0 && files.contains(point);
			}
			actions(pointer.down(device, finger, point));
		}
		else if (phase == 1)
			actions(pointer.move(device, finger, point));
		else if (phase == 2)
			actions(pointer.up(device, finger, point));
		return true;
	}
};
