// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReplaySaveScreen.h"
#include "ReplayWriter.h"
#include "MapHeader.h"
#include "GlobalContainer.h"
#include "gui/PhoneForm.h"
#include <Toolkit.h>
#include <StringTable.h>

std::string replayFilenameToName(const std::string& filename);

ReplaySaveScreen::ReplaySaveScreen(ReplayWriter& writer)
    : writer(writer), dialog("replays", "replay", false,
        GAGCore::Toolkit::getStringTable()->getString("[save replay]"), "",
        replayFilenameToName, glob2NameToFilename)
{
    enablePhoneForm();
}
ReplaySaveScreen::~ReplaySaveScreen() = default;

void ReplaySaveScreen::beginExecution(GAGCore::DrawableSurface* surface)
{
    // The overlay retains ownership of its widgets and offscreen surface.
    Screen::beginExecution(surface);
    if (usesResponsiveViewport())
        form = std::make_unique<PhoneForm>(dialog,
            [](auto*) { return std::string{}; }, [](auto*) { return true; });
}

void ReplaySaveScreen::updateExecution(Uint32 tick)
{
    if (!isExecutionRunning()) return;
    dialog.dispatchTimer(tick);
    const bool wasSaving = dialog.isPersisting();
    if (dialog.pollPersistence()) { endExecute(LoadSaveScreen::OK); return; }
    if (wasSaving && !dialog.isPersisting() && form) form->scrollToTop();
    if (dialog.endValue == LoadSaveScreen::CANCEL) { endExecute(LoadSaveScreen::CANCEL); return; }
    if (dialog.endValue != LoadSaveScreen::OK) return;
    if (!*dialog.getName() || !writer.write(dialog.getFileName())) {
        dialog.showSaveFailure();
        if (form) form->scrollToTop();
    }
    else dialog.beginPersistence(GAGCore::ApplicationHost::persistStorage());
}

void ReplaySaveScreen::handleExecutionEvent(SDL_Event event)
{
    if (!isExecutionRunning()) return;
    if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN) {
        Screen::handleExecutionEvent(event);
        if (!isExecutionRunning()) return;
    }
    if (form && form->event(event)) return;
    GAGCore::GraphicContext::translateMouseEvent(&event);
    dialog.translateAndProcessEvent(&event);
}

void ReplaySaveScreen::drawExecution()
{
    if (!isExecutionRunning()) return;
    paint();
    if (form) form->draw();
    else {
        dialog.dispatchPaint();
        globalContainer->gfx->drawSurface(dialog.decX, dialog.decY, dialog.getSurface());
    }
    globalContainer->gfx->nextFrame();
}

void ReplaySaveScreen::cancelExecutionInput()
{
    if (form) form->cancel();
}

void ReplaySaveScreen::viewportResized(int oldWidth, int oldHeight, int width, int height)
{
    cancelExecutionInput();
    dialog.viewportResized(oldWidth, oldHeight, width, height);
}
