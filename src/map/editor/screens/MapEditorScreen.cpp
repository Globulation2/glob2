// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapEditorScreen.h"
#include "MapEdit.h"
#include "MessageScreen.h"
#include "EditorLoadScreen.h"
#include "OnlineMapsScreen.h"
#include "SettingsScreen.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <stdexcept>
#include <algorithm>

MapEditorScreen::MapEditorScreen(GAGGUI::ScreenStack &screens, std::unique_ptr<MapEdit> editor)
	: screens(screens), editor(std::move(editor))
{
	if (!this->editor)
		throw std::invalid_argument("Map editor screen requires an editor");
}
MapEditorScreen::~MapEditorScreen()
{
	completeInputReceipt(0);
}
void MapEditorScreen::beginAdditionalInput(std::shared_ptr<GAGCore::InputDispatchReceipt> receipt)
{
	inputReceipt = std::move(receipt);
	receiptFirst = input.events().size();
	receiptCount = 0;
}
void MapEditorScreen::endAdditionalInput(std::size_t enqueued)
{
	receiptCount = enqueued;
}
void MapEditorScreen::completeInputReceipt(std::size_t processed)
{
	if (!inputReceipt)
		return;
	inputReceipt->consumed =
		processed <= receiptFirst ? 0 : std::min(receiptCount, processed - receiptFirst);
	inputReceipt.reset();
}
void MapEditorScreen::updateExecution(Uint32 tick)
{
	if (!isExecutionRunning())
		return;
	lastFrame = tick;
	if (!started)
	{
		editor->beginEditing();
		started = true;
	}
	if (inputReceipt && inputReceipt->isCancelled())
	{
		// Preserve unrelated human events while removing cancelled remote work.
		input.erase(receiptFirst, receiptCount);
		completeInputReceipt(0);
	}
	std::size_t processed = 0;
	const bool running = editor->advanceEditing(input.events(), tick, &processed);
	completeInputReceipt(processed);
	input.clear();
	if (!running)
	{
		endExecute(editor->editingReturnCode());
		return;
	}
	if (editor->takeSettingsRequest()) {
        editor->suspendInput();
        screens.push(std::make_unique<SettingsScreen>(&screens));
        return;
    }
	const auto shared = editor->takeShareRequest();
	if (!shared.empty())
	{
		// Sharing (sign-in, upload, catalog details) is a full online screen;
		// closing it returns here with the result shown over the map.
		editor->suspendInput();
		screens.push(std::make_unique<MapShareScreen>(shared),
					 [this](GAGGUI::Screen &, int result) { editor->finishShare(result == MapShareScreen::SHARED); });
	}
	const auto replacement = editor->takeLoadRequest();
	if (!replacement.empty())
	{
		editor->suspendInput();
		screens.push(std::make_unique<EditorLoadScreen>(replacement),
					 [this](GAGGUI::Screen &loading, int result)
					 {
						 if (result == 1)
						 {
							 editor = static_cast<EditorLoadScreen &>(loading).takeEditor();
							 started = false;
							 input.clear();
						 }
						 else if (result == 2)
						 {
							 auto &strings = *GAGCore::Toolkit::getStringTable();
							 screens.push(std::make_unique<MessageScreen>(
								 strings.getString("[ERROR_CANT_LOAD_MAP]"),
								 std::vector<std::string>{strings.getString("[ok]")}));
						 }
					 });
	}
}
bool MapEditorScreen::interceptsQuit() const
{
	// Unsaved work turns a window close into the editor's own quit prompt.
	return editor && editor->hasUnsavedChanges();
}
void MapEditorScreen::handleExecutionEvent(SDL_Event event)
{
	if (isExecutionRunning())
		input.push_back(event);
}
void MapEditorScreen::drawExecution()
{
	if (started && isExecutionRunning())
		editor->drawEditing();
}
Uint32 MapEditorScreen::executionDelay(Uint32 now, Uint32)
{
	const Uint32 elapsed = now - lastFrame;
	// Touch content coasting or bouncing frames at 16 ms so it stays smooth.
	const Uint32 budget = editor && editor->touchAnimating() ? 16 : 33;
	return elapsed < budget ? budget - elapsed : 0;
}

bool MapEditorScreen::usesResponsiveViewport() const
{
	return editor ? editor->wantsResponsiveViewport() : GAGCore::phonePresentationRequested();
}

void MapEditorScreen::viewportResized(int oldWidth, int oldHeight, int width, int height)
{
	editor->suspendInput();
    completeInputReceipt(0);
	input.clear();
	editor->viewportResized(oldWidth, oldHeight, width, height);
}

void MapEditorScreen::suspendExecution()
{
	completeInputReceipt(0);
	editor->suspendInput();
	input.clear();
}

std::vector<GAGGUI::ui::Host *> MapEditorScreen::inspectionHosts()
{
	return editor ? editor->inspectionHosts() : std::vector<GAGGUI::ui::Host *>{};
}
