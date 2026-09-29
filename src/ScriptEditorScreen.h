// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once
#include <set>

#include <GUIBase.h>
#include <memory>
#include <HostViewport.h>
namespace GAGGUI
{
class TextArea;
class Text;
class TextButton;
class TextInput;
} // namespace GAGGUI
using namespace GAGGUI;
class Game;
class LoadSaveScreen;
class MapScript;
class MapScriptSGSL;

class ScriptEditorScreen : public OverlayScreen
{
  public:
	enum
	{
		OK = 0,
		CANCEL = 1,
		COMPILE = 2,
		LOAD,
		SAVE,
		TAB_SCRIPT = 10,
		TAB_OBJECTIVES = 11,
		TAB_BRIEFING = 12,
		TAB_HINTS = 13,
		TAB_PRIMARY = 14,
		TAB_SECONDARY = 15,
		HIDE_KEYBOARD = 16,
		PREVIOUS_ENTRY = 17,
		NEXT_ENTRY = 18,
	};

  protected:
	TextArea *scriptEditor;
	Text *compilationResult;
	MapScriptSGSL *sgslMapScript;
	MapScript *mapScript;
	Game *game;
	Text *mode;
	Text *cursorPosition;
	TextInput *primaryObjectives[8];
	TextInput *secondaryObjectives[8];
	Text *primaryObjectiveLabels[8];
	Text *secondaryObjectiveLabels[8];
	TextArea *missionBriefing;
	TextInput *hints[8];
	Text *hintLabels[8];

	std::vector<Widget *> scriptWidgets;
	std::vector<Widget *> objectivesWidgets;
	std::vector<Widget *> briefingWidgets;
	std::vector<Widget *> hintWidgets;

	bool changeTabAgain;

  protected:
	bool testCompile(void);

  public:
	ScriptEditorScreen(Game *game);
	~ScriptEditorScreen() override;
	void translateAndProcessEvent(SDL_Event *event) override;
	void drawFileDialog();
	OverlayScreen *phoneDialog();
	void drawTouch();
	// Hosts and visual fixtures can supply a keyboard-reduced viewport without
	// resizing the underlying world or replacing any editing drafts.
	void drawTouchInViewport(GAGCore::ViewRect available, bool keyboardVisible);
	bool eventTouch(SDL_Event event);
	void cancelTouch();
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	virtual void onSDLEvent(SDL_Event *event);
	virtual void onTimer(Uint32 tick);

  private:
	friend struct ScriptEditorTouchTestAccess;
	void loadSave(bool isLoad, const char *dir, const char *ext);
	void finishFileDialog();
	std::unique_ptr<LoadSaveScreen> fileDialog;
	bool loadingScript = false;
	struct TouchControl
	{
		GAGCore::ViewRect bounds;
		int action;
	};
	std::vector<TouchControl> touchControls;
	GAGCore::ViewRect touchBounds, touchContent;
	int touchTab = TAB_SCRIPT, touchItem = 0;
	bool touchSecondary = false;
	bool compactKeyboardWorkspace = false;
	int heldTouchAction = -1;
	std::set<std::pair<SDL_TouchID, SDL_FingerID>> touchPointers;
	bool touchInterrupted = false;
	GAGCore::ViewPoint touchDown;
	bool touchMoving = false;
	std::string touchPreedit;
	TextArea *touchEntry = nullptr;
	TextInput *touchEntryTarget = nullptr;
	void flushTouchEntry();
	void prepareTouch();
	void prepareTouch(GAGCore::ViewRect available, bool keyboardVisible);
	void touchAction(int action);
	void deactivateTouchText();
};
