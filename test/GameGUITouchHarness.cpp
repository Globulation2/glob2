// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"
#include "GameGUITouch.h"
#include "InGameTouchTheme.h"
#include "GameGUIDialog.h"
#include "GameGUILoadSave.h"
#include "GameGUIInternal.h"
#include "GameUtilities.h"
#include <GUITextInput.h>
#include <GUIButton.h>
#include <GUISelector.h>
#include <Toolkit.h>
#include <StringTable.h>
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "ReplayWriter.h"
#include "ReplayReader.h"
#include "usl.h"
#include "native.h"
#include <ResponsiveDialog.h>
#include "PhoneForm.h"
#include "CampaignSelectorScreen.h"
#include "CampaignMenuScreen.h"
#include "ChooseMapScreen.h"
#include "CustomGameScreen.h"
#include "NewMapScreen.h"
#include "SettingsScreen.h"
#include "EndGameScreen.h"

#include "MapEdit.h"
#include "MapEditorScreen.h"
#include "CampaignEditor.h"
#include "PhoneEditor.h"
#include "EditorFileView.h"
#include "MapEditDialog.h"
#include <GUITextArea.h>
#include <GUIRatio.h>
#include <ScreenStack.h>
#include <BinaryStream.h>
#include <filesystem>
#include <fstream>
#include <SDL_net.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

GlobalContainer* globalContainer=nullptr;
static void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
class GameGUITouchHarness
{
public:
  static void editorInteractions()
  {
	  MapEdit editor;
	  require(editor.load("maps/balanced.map"), "Editor touch fixture loads");
	  editor.phone = std::make_unique<PhoneEditor>(editor);
	  auto &touch = *editor.phone;
	  auto *gfx = globalContainer->gfx;
	  touch.chooseMode(0);
	  touch.prepare();
	  editor.updateCamera();
	  auto finger = [&](Uint32 kind, int id, GAGCore::ViewPoint p)
	  {
		  SDL_Event event{};
		  event.type = kind;
		  event.tfinger.touchId = 19;
		  event.tfinger.fingerId = id;
		  event.tfinger.x = p.x / gfx->getW();
		  event.tfinger.y = p.y / gfx->getH();
		  touch.event(event);
	  };
	  const GAGCore::ViewPoint start{touch.content.x + 96, touch.content.y + 48};
	  const GAGCore::ViewPoint finish{start.x + 64, start.y + 32};
	  editor.performAction("select sand");
	  auto checksum = [&] { return editor.game.checkSum(nullptr, nullptr, nullptr, true); };
	  auto before = checksum();
	  finger(SDL_FINGERDOWN, 1, start);
	  finger(SDL_FINGERMOTION, 1, finish);
	  touch.draw();
      require(checksum() == before, "Editor paint preview mutated terrain before completing the stroke");
	  finger(SDL_FINGERUP, 1, finish);
	  require(checksum() != before, "Completed editor paint stroke failed to apply");
	  editor.performAction("select water");
	  before = checksum();
	  finger(SDL_FINGERDOWN, 1, start);
	  finger(SDL_FINGERMOTION, 1, finish);
	  finger(SDL_FINGERDOWN, 2, {finish.x + 48, finish.y});
	  finger(SDL_FINGERUP, 2, {finish.x + 48, finish.y});
	  finger(SDL_FINGERUP, 1, finish);
	  require(checksum() == before, "Second finger committed an unfinished editor paint stroke");
	  finger(SDL_FINGERDOWN, 1, start);
	  finger(SDL_FINGERMOTION, 1, finish);
	  SDL_Event focus{};
	  focus.type = SDL_WINDOWEVENT;
	  focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
	  touch.event(focus);
	  finger(SDL_FINGERUP, 1, finish);
	  require(checksum() == before, "Focus loss committed an unfinished editor paint stroke");
	  touch.chooseMode(2);
	  touch.prepare();
	  editor.updateCamera();
	  const auto icon = touch.rows[1].rect; // inn, using the real palette hit area
	  const GAGCore::ViewPoint source{icon.x + icon.w / 2, icon.y + icon.h / 2};
	  const int type =
		  globalContainer->buildingsTypes.getTypeNum("inn", editor.buildingLevel, false);
	  auto *building = globalContainer->buildingsTypes.get(type);
	  // The bundled map may open over sea; put a known legal footprint in
	  // view before exercising the gesture, without changing map contents.
	  bool room = false;
	  for (int y = 0; y < editor.game.map.getH() && !room; ++y)
		  for (int x = 0; x < editor.game.map.getW(); ++x)
		  {
			  int bx, by;
			  if (editor.game.checkRoomForBuilding(x, y, building, &bx, &by, editor.team, false))
			  {
				  int dx, dy;
				  editor.game.map.cursorToBuildingPos(
					  editor.mapMouseX(touch.content.x + touch.content.w / 2),
					  editor.mapMouseY(touch.content.y + touch.content.h / 2 -
									   40 * gfx->logicalUnitsPerPoint()),
					  building->width, building->height, &dx, &dy, 0, 0);
				  editor.viewportX = (x - dx) & editor.game.map.wMask;
				  editor.viewportY = (y - dy) & editor.game.map.hMask;
				  room = true;
				  break;
			  }
		  }
	  require(room, "Editor map has no legal building footprint");
	  editor.updateCamera();
	  GAGCore::ViewPoint destination{};
	  bool found = false;
	  for (int y = touch.content.y + 48 * gfx->logicalUnitsPerPoint();
		   y < touch.content.y + touch.content.h - 8 && !found; y += 8)
		  for (int x = touch.content.x + 16; x < touch.content.x + touch.content.w - 16; x += 8)
		  {
			  int mx, my, bx, by;
			  editor.game.map.cursorToBuildingPos(
				  editor.mapMouseX(x), editor.mapMouseY(y - 40 * gfx->logicalUnitsPerPoint()),
				  building->width, building->height, &mx, &my, editor.viewportX, editor.viewportY);
			  if (editor.game.checkRoomForBuilding(mx, my, building, &bx, &by, editor.team, false))
			  {
				  destination = {double(x), double(y)};
				  found = true;
				  break;
			  }
		  }
	  require(found, "No visible valid editor placement fixture");
	  auto count = [&]
	  {
		  int count = 0;
		  for (int t = 0; t < editor.game.mapHeader.getNumberOfTeams(); ++t)
			  for (int b = 0; b < Building::MAX_COUNT; ++b)
				  count += editor.game.teams[t]->myBuildings[b] != nullptr;
		  return count;
	  };
	  auto drag = [&](GAGCore::ViewPoint end)
	  {
		  finger(SDL_FINGERDOWN, 1, source);
		  finger(SDL_FINGERMOTION, 1, destination);
		  finger(SDL_FINGERUP, 1, end);
	  };
	  const int countBefore = count();
	  drag(destination);
	  require(count() == countBefore + 1,
			  "Editor valid building drag must place exactly one building");
	  drag(destination);
	  require(count() == countBefore + 1, "Editor invalid occupied drop placed another building");
	  drag(source);
	  require(count() == countBefore + 1, "Editor UI drop placed a building");
	  finger(SDL_FINGERDOWN, 1, source);
	  finger(SDL_FINGERMOTION, 1, destination);
	  finger(SDL_FINGERDOWN, 2, start);
	  finger(SDL_FINGERUP, 1, destination);
	  finger(SDL_FINGERUP, 2, start);
	  require(count() == countBefore + 1, "Second finger committed an interrupted editor drag");
      auto tap=[&](GAGCore::ViewPoint p) {finger(SDL_FINGERDOWN,1,p);finger(SDL_FINGERUP,1,p);};
      touch.chooseMode(0);editor.performAction("select water");touch.prepare();
      const double u=gfx->logicalUnitsPerPoint();
      tap({touch.safe.x+touch.safe.w*7/8,touch.safe.y+22*u});
      require(editor.selectionMode==MapEdit::PlaceNothing && !touch.pan,"Done did not return editor to selection");
      editor.performAction("select sand");
      tap({touch.safe.x+touch.safe.w*3/8,touch.safe.y+22*u});
      require(touch.brushOpen,"Brush control did not open visual brush choices");
      tap({touch.brushPanel.x+touch.brushPanel.w*7/8,touch.brushPanel.y+84*u});
      require(editor.brush.getFigure()==7 && !touch.brushOpen,"Visual brush choice did not apply");
      touch.chooseMode(2);
      editor.mouseX=destination.x;editor.mouseY=destination.y-40*u;
      editor.performAction("select map building");touch.prepare();
      require(touch.inspecting() && !touch.properties.empty(),"Selected building has no contextual property rows");
      auto property=touch.properties.front();const int oldValue=property.value->currentValue();
      editor.hasMapBeenModified=false;
      tap({property.rect.x+22*u,property.rect.y+46*u});
      require(property.value->currentValue()==std::max(0,oldValue-1) && editor.hasMapBeenModified,"Inspector step did not change named property or mark draft modified");
      tap({touch.inspector.x+touch.inspector.w-26*u,touch.inspector.y+26*u});
      require(!touch.inspecting() && touch.paletteMode==2,"Closing inspector did not restore palette");
	  std::puts("PASS: editor buffered paint, focus/second-finger cancellation, one-building drag "
				"and invalid/UI drops");
  }

  static void editorAreaNameInteractions()
  {
      AskForTextInput area("[Change Area Name]", "Northern passage");
      area.drawTouch();
      const double u=globalContainer->gfx->logicalUnitsPerPoint();
      const GAGCore::ViewRect shortSafe{0,0,double(globalContainer->gfx->getW()),120*u};
      area.prepareTouch(shortSafe);
      for(const auto r:{area.touchInput,area.touchCancel,area.touchConfirm})
          require(r.y>=shortSafe.y && r.y+r.h<=shortSafe.y+shortSafe.h && r.h>=44*u,
                  "Area keyboard-clearance controls escape 120pt safe viewport");
      require(area.touchInput.y+area.touchInput.h<=area.touchCancel.y,
              "Area keyboard-clearance input overlaps actions");
      area.prepareTouch();

      SDL_Event composition{}; composition.type=SDL_TEXTEDITING;
      std::strcpy(composition.edit.text,"\xC3\xA9");area.eventTouch(composition);
      SDL_Event enter{};enter.type=SDL_KEYDOWN;enter.key.keysym.sym=SDLK_RETURN;
      area.eventTouch(enter);
      require(area.endValue==-1 && area.getText()=="Northern passage", "Area IME submitted provisional text");
      SDL_Event text{};text.type=SDL_TEXTINPUT;std::strcpy(text.text.text,"\xC3\xA9");area.eventTouch(text);
      auto *gfx=globalContainer->gfx;
      SDL_Event finger{};finger.type=SDL_FINGERDOWN;finger.tfinger.touchId=31;finger.tfinger.fingerId=1;
      finger.tfinger.x=(area.touchConfirm.x+area.touchConfirm.w/2)/gfx->getW();
      finger.tfinger.y=(area.touchConfirm.y+area.touchConfirm.h/2)/gfx->getH();area.eventTouch(finger);
      finger.type=SDL_FINGERUP;area.eventTouch(finger);
      require(area.endValue==AskForTextInput::OK && area.getText()=="Northern passage\xC3\xA9", "Area touch confirmation lost native UTF-8 edits");
      AskForTextInput cancelled("[Change Area Name]", "Original");cancelled.eventTouch(text);
      enter.key.keysym.sym=SDLK_ESCAPE;cancelled.eventTouch(enter);
      require(cancelled.endValue==AskForTextInput::CANCEL && cancelled.getText()=="Original", "Cancelling area name committed draft");
      std::cout << "PASS: editor area name IME, touch confirmation and cancellation\n";
  }

  static void editorFileInteractions()
  {
      auto *gfx=globalContainer->gfx;
      auto finger=[&](EditorFileView &view,Uint32 kind,int id,GAGCore::ViewPoint p) {
          SDL_Event event{};event.type=kind;event.tfinger.touchId=29;event.tfinger.fingerId=id;
          event.tfinger.x=p.x/gfx->getW();event.tfinger.y=p.y/gfx->getH();view.event(event);
      };
      auto tap=[&](EditorFileView &view,GAGCore::ViewRect r) {
          const GAGCore::ViewPoint p{r.x+r.w/2,r.y+r.h/2};
          finger(view,SDL_FINGERDOWN,1,p);finger(view,SDL_FINGERUP,1,p);
      };
      LoadSaveScreen save("maps","map",false,"Save map","touch-file-fixture",glob2FilenameToName,glob2NameToFilename);
      EditorFileView saveView(save);saveView.draw();
      const double u=gfx->logicalUnitsPerPoint();
      for(double height:{120.,80.}) {
          const GAGCore::ViewRect safe{0,0,double(gfx->getW()),height*u};
          saveView.prepare(&safe);
          for(const auto r:{saveView.filename,saveView.cancelButton,saveView.primaryButton})
              require(r.y>=safe.y && r.y+r.h<=safe.h && r.h>=44*u,
                      "File keyboard-clearance controls escape safe viewport");
          const auto a=saveView.filename,b=saveView.cancelButton;
          require(a.y+a.h<=b.y || a.x+a.w<=b.x,
                  "File keyboard-clearance input overlaps footer actions");
      }
      saveView.prepare();

      tap(saveView,saveView.filename);
      SDL_Event composition{};composition.type=SDL_TEXTEDITING;
      std::strcpy(composition.edit.text,"\xC3\xA9");saveView.event(composition);
      SDL_Event enter{};enter.type=SDL_KEYDOWN;enter.key.keysym.sym=SDLK_RETURN;saveView.event(enter);
      require(save.endValue==-1 && std::string(save.getName())=="touch-file-fixture",
              "Filename IME confirmation submitted a save or committed provisional text");
      SDL_Event text{};text.type=SDL_TEXTINPUT;std::strcpy(text.text.text,"\xC3\xA9");saveView.event(text);
      const std::string draft="touch-file-fixture\xC3\xA9";
      require(std::string(save.getName())==draft,"Native filename input lost its UTF-8 committed text");
      saveView.prepare();tap(saveView,saveView.primaryButton);
      require(save.endValue==LoadSaveScreen::OK &&
              std::string(save.getFileName())==glob2NameToFilename("maps",draft,"map"),
              "Touch save did not use the existing filename conversion and confirmation path");
      save.showSaveFailure();saveView.draw();
      require(save.filePresentation().failed && !save.filePresentation().status.empty() &&
              std::string(save.getName())==draft,"Save failure lost its draft or visible retry state");
      tap(saveView,saveView.primaryButton);
      require(save.endValue==LoadSaveScreen::OK,"Retry did not reuse the original save command");
      struct Pending final:GAGCore::ApplicationHost::Persistence {
          GAGCore::ApplicationHost::PersistenceState value=GAGCore::ApplicationHost::PersistenceState::Pending;
          GAGCore::ApplicationHost::PersistenceState state() const override {return value;}
      };
      auto pending=std::make_unique<Pending>();auto *operation=pending.get();
      save.beginPersistence(std::move(pending));saveView.draw();
      tap(saveView,saveView.filename);saveView.event(text);tap(saveView,saveView.cancelButton);
      require(save.endValue==-1 && std::string(save.getName())==draft && !save.pollPersistence(),
              "Busy file dialog accepted editing/cancellation or completed a pending save");
      operation->value=GAGCore::ApplicationHost::PersistenceState::Failed;
      require(!save.pollPersistence() && save.filePresentation().failed,
              "Persistence failure did not return the file view to its retry state");

      LoadSaveScreen load("maps","map",true,"Load map",nullptr,glob2FilenameToName,glob2NameToFilename);
      EditorFileView loadView(load);loadView.draw();
      require(loadView.maximum>0 && loadView.files.h>=44*loadView.unit,
              "File interaction fixture requires a scrollable list with full touch rows");
      const GAGCore::ViewPoint bottom{loadView.files.x+20*loadView.unit,loadView.files.y+loadView.files.h-10*loadView.unit};
      const GAGCore::ViewPoint top{bottom.x,loadView.files.y+10*loadView.unit};
      finger(loadView,SDL_FINGERDOWN,1,bottom);finger(loadView,SDL_FINGERMOTION,1,top);
      finger(loadView,SDL_FINGERUP,1,top);
      require(loadView.offset>0 && load.filePresentation().selected==-1 && load.endValue==-1,
              "Swiping the file list selected a map or confirmed a load");
      const int index=int(std::ceil(loadView.offset/(44*loadView.unit)));
      require(index<int(loadView.model.files.size()),"Scrolled file fixture has no visible row");
      const auto expected=loadView.model.files[index];
      const GAGCore::ViewRect row{loadView.files.x,loadView.files.y+index*44*loadView.unit-loadView.offset,
                                 loadView.files.w,44*loadView.unit};
      tap(loadView,row);
      require(std::string(load.getName())==expected && load.endValue==-1,
              "Tapping a file row did not select its semantic filename without loading");
      loadView.prepare();tap(loadView,loadView.primaryButton);
      require(load.endValue==LoadSaveScreen::OK,"Explicit Load did not confirm the selected file");
      std::puts("PASS: editor file view UTF-8/IME, shared filename commands, busy/error retry and scrolling");
  }
  static void run()
  {
	  {
		  Usl interpreter;
		  auto *constant = new NativeValue<int>(&interpreter.heap, 42);
		  interpreter.setConstant("retained", constant);
		  for (int cycle = 0; cycle < 100; ++cycle)
		  {
			  interpreter.run(1);
			  require(std::find(interpreter.heap.values.begin(), interpreter.heap.values.end(),
								constant) != interpreter.heap.values.end(),
					  "Repeated script collection retains bridge constants");
			  require(interpreter.getConstant("retained") == constant,
					  "Script constant remains accessible");
		  }
	  }
	  for (double width : {320., 568., 768.})
		  for (double height : {160., 320., 568.})
		  {
			  std::vector<bool> footers{false, false, true, true, true};
			  auto layout = GAGCore::ResponsiveDialog::calculate(
				  {0, 24, width, height - 24}, footers,
				  [](size_t i, double w) { return i == 2 ? (w < 200 ? 96. : 48.) : 48.; }, 10000);
			  require(layout.offset == layout.maximum,
					  "Dialog scrolling clamps to reachable content");
			  require(layout.content.h >= 48, "Short dialog keeps room for scrollable controls");
			  for (const auto &row : layout.rows)
				  if (row.footer)
				  {
					  require(row.rect.y >= 24 && row.rect.y + row.rect.h <= height,
							  "Fixed actions respect safe bounds");
					  require(row.rect.h >= 48, "Fixed actions preserve minimum touch height");
				  }
		  }
	  GameGUI gui;
	  auto map = Engine::loadMapHeader("maps/balanced.map");
	  GameHeader players;
	  players.setNumberOfPlayers(1);
	  players.getBasePlayer(0) = BasePlayer(0, "Touch", 0, BasePlayer::P_LOCAL);
	  require(gui.loadFromHeaders(map, players, true, true), "Fixture load failed");
	  gui.localTeamNo = 0;
	  gui.localPlayer = 0;
	  gui.adjustLocalTeam();
	  gui.viewportX = gui.viewportY = 0;
	  {
		  globalContainer->replayWriter = std::make_unique<ReplayWriter>();
		  auto &writer = *globalContainer->replayWriter;
		  writer.init("", gui);
		  require(writer.write("replays/touch-empty.replay"),
				  "An unfinished in-memory replay can be exported");
		  {
			  ReplayReader empty;
			  require(empty.loadReplay("replays/touch-empty.replay"),
					  "Replay export retains the final buffered header byte");
		  }
		  for (int i = 0; i < 100; ++i)
			  writer.advanceStep();
		  writer.finish();
		  require(writer.write("replays/touch-preview.replay"), "Replay fixture writes");
		  const auto position = writer.getBuffer()->getPosition();
		  const auto blocked =
			  std::filesystem::path(SDL_getenv("GLOB2_USER_DATA_DIR")) / "replays/blocked.replay";
		  std::filesystem::create_directories(blocked);
		  {
			  std::ofstream marker(blocked / "keep");
			  marker << "preserve";
		  }
		  require(!writer.write(blocked.string()),
				  "Replay replacement failure returns false without asserting");
		  require(std::filesystem::exists(blocked / "keep") &&
					  writer.getBuffer()->getPosition() == position,
				  "Failed replay write preserves destination and live buffer position");
		  std::filesystem::remove(blocked / "keep");
		  std::filesystem::remove(blocked);
		  require(writer.write(blocked.string()) && writer.getBuffer()->getPosition() == position,
				  "Replay retries after a failed replacement");
		  std::ifstream original(std::filesystem::path(SDL_getenv("GLOB2_USER_DATA_DIR")) /
									 "replays/touch-preview.replay",
								 std::ios::binary);
		  std::ifstream retried(blocked, std::ios::binary);
		  require(std::string(std::istreambuf_iterator<char>(original), {}) ==
					  std::string(std::istreambuf_iterator<char>(retried), {}),
				  "Replay retry produces identical bytes");
		  original.close();
		  retried.close();
		  std::filesystem::remove(blocked);
	  }

	  const auto checksum = gui.game.checkSum();
	  auto finger = [&](Uint32 type, int id, float x, float y)
	  {
		  SDL_Event event{};
		  event.type = type;
		  event.tfinger.touchId = 7;
		  event.tfinger.fingerId = id;
		  event.tfinger.x = x / globalContainer->gfx->getW();
		  event.tfinger.y = y / globalContainer->gfx->getH();
		  gui.processEvent(&event);
	  };
	  auto tap = [&](float x, float y)
	  {
		  finger(SDL_FINGERDOWN, 1, x, y);
		  finger(SDL_FINGERUP, 1, x, y);
	  };
	  auto flag = [&] { gui.setSelection(GameGUI::TOOL_SELECTION, const_cast<char *>("warflag")); };
	  auto noOrder = [&]
	  { require(!gui.toolManager.getOrder(), "Navigation or preview emitted a tool order"); };
	  tap(760, 208);
	  require(gui.selectionMode == GameGUI::TOOL_SELECTION &&
				  gui.toolManager.getBuildingName() == "inn",
			  "A tool must be selectable from its real sidebar hit area without mouse hover");
	  noOrder();
	  gui.clearSelection();
	  finger(SDL_FINGERDOWN, 1, 160, 160);
	  finger(SDL_FINGERMOTION, 1, 166, 160);
	  finger(SDL_FINGERUP, 1, 166, 160);
	  require(gui.viewportX == 0, "Sub-threshold movement must not pan");
	  finger(SDL_FINGERDOWN, 1, 160, 160);
	  finger(SDL_FINGERMOTION, 1, 224, 160);
	  finger(SDL_FINGERUP, 1, 224, 160);
	  require(gui.viewportX == gui.game.map.getW() - 2,
			  "Dragging must pan across the toroidal seam");
	  flag();
	  tap(200, 200);
	  noOrder();
	  require(gui.touch->hasPreview(), "Placement tap must retain a preview");
	  const int before = gui.viewportX;
	  finger(SDL_FINGERDOWN, 1, 200, 200);
	  finger(SDL_FINGERDOWN, 2, 300, 200);
	  finger(SDL_FINGERMOTION, 1, 264, 200);
	  finger(SDL_FINGERMOTION, 2, 364, 200);
	  finger(SDL_FINGERUP, 2, 364, 200);
	  finger(SDL_FINGERUP, 1, 264, 200);
	  require(gui.viewportX == ((before - 2) & gui.game.map.getMaskW()),
			  "Two fingers must pan while placing");
	  noOrder();
	  tap(100, 576);
	  auto order = std::dynamic_pointer_cast<OrderCreate>(gui.toolManager.getOrder());
	  require(bool(order), "Confirmation must emit the shared create order");
	  require(gui.selectionMode == GameGUI::NO_SELECTION,
			  "Confirmed placement must exit preview mode");
	  noOrder();
	  gui.clearSelection();
	  const double oldZoom = gui.camera.zoom;
	  finger(SDL_FINGERDOWN, 1, 200, 200);
	  finger(SDL_FINGERDOWN, 2, 300, 200);
	  finger(SDL_FINGERMOTION, 2, 350, 200);
	  finger(SDL_FINGERUP, 2, 350, 200);
	  finger(SDL_FINGERUP, 1, 200, 200);
	  require(std::abs(gui.camera.zoom - oldZoom * 1.5) < 0.001,
			  "Pinch uses the shared camera zoom");
	  noOrder();
	  flag();
	  tap(240, 240);
	  const auto position = gui.camera.screenToWorld(240, 240);
	  require(gui.touch->preview && std::abs(gui.touch->preview->x - position.first) < 1.01 &&
				  std::abs(gui.touch->preview->y - position.second) < 1.01,
			  "Placement preview tracks world coordinates after fractional pan and zoom");
	  tap(480, 576);
	  noOrder();
	  flag();
	  tap(200, 200);
	  tap(480, 576);
	  noOrder();
	  require(gui.selectionMode == GameGUI::NO_SELECTION, "Cancel must exit placement");
	  flag();
	  tap(220, 220);
	  // The first point is in Confirm, the second just outside the strip.
	  finger(SDL_FINGERDOWN, 1, 100, 554);
	  finger(SDL_FINGERUP, 1, 100, 550);
	  noOrder();
	  require(gui.touch->hasPreview(), "Crossing a control boundary must not place or cancel");
	  finger(SDL_FINGERDOWN, 1, 100, 576);
	  gui.suspendInput();
	  finger(SDL_FINGERUP, 1, 100, 576);
	  noOrder();
	  require(gui.touch->hasPreview(), "Suspension retains preview but cancels held confirmation");
	  tap(220, 220);
	  gui.localTeam->noMoreBuildingSitesCountdown = 1;
	  tap(100, 576);
	  noOrder();
	  require(gui.touch->hasPreview(), "Failed validation must retain the preview");
	  gui.localTeam->noMoreBuildingSitesCountdown = 0;
	  gui.suspendInput();
	  flag();
	  finger(SDL_FINGERDOWN, 1, 200, 200);
	  gui.clearSelection();
	  finger(SDL_FINGERUP, 1, 200, 200);
	  noOrder();
	  require(!gui.touch->hasPreview(), "Mode changes must cancel an owned gesture");
	  flag();
	  tap(200, 200);
	  SDL_Event synthetic{};
	  synthetic.type = SDL_MOUSEBUTTONUP;
	  synthetic.button.which = SDL_TOUCH_MOUSEID;
	  synthetic.button.button = SDL_BUTTON_LEFT;
	  synthetic.button.x = 200;
	  synthetic.button.y = 200;
	  gui.step({synthetic}, SDL_GetTicks64());
	  require(gui.getOrder()->getOrderType() == ORDER_NULL,
			  "Synthesized mouse release must not place");
	  noOrder();
	  gui.suspendInput();
	  gui.setSelection(GameGUI::BRUSH_SELECTION);
	  gui.toolManager.activateZoneTool(GameGUIToolManager::Forbidden);
	  gui.brush.defaultSelection();
	  finger(SDL_FINGERDOWN, 1, 200, 200);
	  finger(SDL_FINGERMOTION, 1, 232, 200);
	  finger(SDL_FINGERDOWN, 2, 300, 200);
	  noOrder(); // A second finger cancels the unfinished stroke.
	  finger(SDL_FINGERMOTION, 1, 296, 200);
	  finger(SDL_FINGERUP, 2, 300, 200);
	  finger(SDL_FINGERUP, 1, 296, 200);
	  noOrder();
	  finger(SDL_FINGERDOWN, 1, 240, 240);
	  gui.suspendInput();
	  noOrder(); // Focus suspension must not commit an unfinished stroke.
	  finger(SDL_FINGERUP, 1, 240, 240);
	  noOrder();
	  flag();
	  tap(220, 220);
	  globalContainer->replaying = true;
	  tap(100, 576);
	  noOrder();
	  globalContainer->replaying = false;
	  gui.suspendInput();
	  flag();
	  tap(200, 200);
	  finger(SDL_FINGERDOWN, 1, 100, 576);
	  gui.viewportResized(800, 600, 600, 800);
	  finger(SDL_FINGERUP, 1, 100, 576);
	  noOrder();
	  require(gui.touch->hasPreview(), "Rotation retains preview but cancels held confirmation");
	  finger(SDL_FINGERDOWN, 1, 200, 200);
	  SDL_Event focus{};
	  focus.type = SDL_WINDOWEVENT;
	  focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
	  gui.processEvent(&focus);
	  finger(SDL_FINGERUP, 1, 200, 200);
	  noOrder();
	  require(!gui.touch->hasPreview(), "Focus loss must clear owned pointers");
	  focus.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
	  gui.processEvent(&focus);
	  tap(200, 200);
	  gui.drawAll(0);
	  globalContainer->gfx->printScreen("touch-placement.bmp");
	  globalContainer->gfx->nextFrame();
	  auto *capture = SDL_LoadBMP(
		  (std::string(SDL_getenv("GLOB2_USER_DATA_DIR")) + "/touch-placement.bmp").c_str());
	  require(capture && capture->format->BytesPerPixel == 4,
			  "Placement screenshot must be captured");
	  Uint8 r, g, b;
	  const auto pixel = static_cast<Uint32 *>(static_cast<void *>(
		  static_cast<char *>(capture->pixels) + (capture->h - 10) * capture->pitch))[10];
	  SDL_GetRGB(pixel, capture->format, &r, &g, &b);
	  SDL_FreeSurface(capture);
	  require(g > r + 20, "Confirm must be visibly drawn over the world");
	  require(gui.game.checkSum() == checksum,
			  "Touch navigation and queued orders must not mutate simulation state");
	  gui.game.map.setMapDiscovered(); // Expose terrain for this rendering fixture only.
	  const auto hudChecksum = gui.game.checkSum();
	  gui.clearSelection();
	  gui.suspendInput();
	  SDL_setenv("GLOB2_TOUCH_HUD", "1", 1);
	  SDL_setenv("GLOB2_MOBILE_UI", "1", 1);
	  auto *gfx = globalContainer->gfx;
	  SDL_setenv("GLOB2_RESPONSIVE_UI", "1", 1);
	  gfx->setResponsiveViewport(true, 800, 600);
	  for (auto [width, height] : {std::pair{320, 568}, {568, 320}})
	  {
		  const int oldW = gfx->getW(), oldH = gfx->getH();
		  SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()), width, height);
		  SDL_Event resize{};
		  resize.type = SDL_WINDOWEVENT;
		  resize.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
		  GAGCore::GraphicContext::translateMouseEvent(&resize);
		  gui.viewportResized(oldW, oldH, gfx->getW(), gfx->getH());
		  tap(100, 200); // Activate touch after the resize cancellation.
		  const float unit = gfx->logicalUnitsPerPoint();
		  gui.clearSelection();
		  gui.displayMode = GameGUI::FLAG_VIEW;
		  tap(gfx->getW() / 12.0f, gfx->getH() - 24 * unit);
		  require(gui.touch->usesHUD(), "Phone HUD must be active");
		  require(gui.displayMode == GameGUI::CONSTRUCTION_VIEW,
				  "Visible Build control routes to construction");
		  gui.drawAll(0);
		  gfx->printScreen(width < height ? "touch-hud-portrait.bmp" : "touch-hud-landscape.bmp");
		  gfx->nextFrame();
		  int targetX, targetY, centerX, centerY;
		  gui.minimap.convertToMap(gfx->getW() - 80, 64, targetX, targetY);
		  gui.minimapMouseToPos(gfx->getW() - 80, 64, &centerX, &centerY, true);
		  const auto world = gui.touch->worldBounds();
		  require(centerX == gui.camera.tileX() && centerY == gui.camera.tileY(),
				  "Minimap navigation uses the shared normalized camera");
		  auto ui = gui.touch->layout();
		  const int cameraX = gui.viewportX, cameraY = gui.viewportY;
		  finger(SDL_FINGERDOWN, 1, ui.panel.x + 2 * unit, ui.panel.y + 70 * unit);
		  finger(SDL_FINGERMOTION, 1, ui.panel.x + 2 * unit, ui.panel.y + 30 * unit);
		  finger(SDL_FINGERUP, 1, ui.panel.x + 2 * unit, ui.panel.y + 30 * unit);
		  require(gui.viewportX == cameraX && gui.viewportY == cameraY,
				  "Panel scrolling must not pan the world");
		  noOrder();
		  const auto inn =
			  std::find(gui.buildingsChoiceName.begin(), gui.buildingsChoiceName.end(), "inn") -
			  gui.buildingsChoiceName.begin();
		  gui.touch->panelScroll = 0;
		  gui.touch->clampScroll();
		  const auto palette = gui.touch->paletteItemRect(inn);
		  tap(palette.x + palette.w / 2, palette.y + palette.h / 2);
		  require(gui.selectionMode == GameGUI::TOOL_SELECTION &&
					  gui.toolManager.getBuildingName() == "inn",
				  "Labeled touch palette must select the same building after rotation");
		  noOrder();
		  gui.clearSelection();
		  gui.scriptText =
			  "Build an inn to feed your workers. Drag the panel to find more buildings. "
			  "Select a building, choose a location, and confirm when you are ready.\n"
			  "This long instruction remains readable after rotating the phone.";
		  gui.swallowSpaceKey = true;
		  gui.setIsSpaceSet(false);
		  gui.drawAll(0);
		  gfx->printScreen(width < height ? "touch-tutorial-portrait.bmp"
										  : "touch-tutorial-landscape.bmp");
		  gfx->nextFrame();
		  finger(SDL_FINGERDOWN, 1, 30 * unit, 80 * unit);
		  finger(SDL_FINGERMOTION, 1, 30 * unit, 60 * unit);
		  finger(SDL_FINGERUP, 1, 30 * unit, 60 * unit);
		  require(!gui.isSpaceSet(), "Scrolling tutorial text must not acknowledge it");
		  const auto tutorial = gui.touch->tutorialRect();
		  tap(tutorial.x + 20 * unit, tutorial.y + tutorial.h - 24 * unit);
		  require(gui.isSpaceSet(), "Tutorial touch acknowledgment uses the shared Space action");
		  gui.scriptText.clear();
		  gui.swallowSpaceKey = false;
	  }
	  require(gui.game.checkSum() == hudChecksum, "HUD interaction must not mutate the simulation");
	  // Both input patterns must reach the identical construction operation.
	  // Flags have no resource cost and are convenient placement fixtures.
	  gui.clearSelection();
	  gui.touch->cancel();
	  gui.displayMode = GameGUI::FLAG_VIEW;
	  gui.touch->panelOpen = true;
	  gui.touch->panelScroll = 0;
	  gui.drawAll(0);
	  gfx->nextFrame();
	  const auto icon = gui.touch->paletteItemRect(1);
	  const float iconX = icon.x + icon.w / 2, iconY = icon.y + icon.h / 2;
	  const float dropX = 120, dropY = 190;
	  finger(SDL_FINGERDOWN, 1, iconX, iconY);
	  require(gui.touch->placement.has_value(), "Palette press starts an owned placement session");
	  noOrder();
	  finger(SDL_FINGERMOTION, 99, dropX, dropY);
	  finger(SDL_FINGERUP, 99, dropX, dropY);
	  require(gui.touch->placement.has_value(),
			  "Unknown contact motion and release do not steal placement ownership");
	  noOrder();
	  finger(SDL_FINGERMOTION, 1, dropX, dropY);
	  noOrder();
	  require(gui.touch->showsBuildPalette(),
			  "Active placement retains palette composition for persistent layouts");
	  gui.drawAll(0);
	  gfx->printScreen("touch-drag-preview.bmp");
	  gfx->nextFrame();
	  finger(SDL_FINGERUP, 1, dropX, dropY);
	  auto dragged = std::dynamic_pointer_cast<OrderCreate>(gui.toolManager.getOrder());
	  require(bool(dragged), "Valid palette drag commits one order on release");
	  noOrder();
	  require(gui.touch->panelOpen && gui.selectionMode == GameGUI::NO_SELECTION,
			  "Drag restores the repeated-placement palette");
	  gui.ghostManager.removeBuilding(dragged->posX, dragged->posY);
	  tap(iconX, iconY);
	  noOrder();
	  require(gui.selectionMode == GameGUI::TOOL_SELECTION,
			  "Palette tap selects preview-and-confirm");
	  tap(dropX, dropY - 48 * gfx->logicalUnitsPerPoint());
	  noOrder();
	  const auto confirm = gui.touch->controls();
	  tap(confirm.x + confirm.w / 4, confirm.y + confirm.h / 2);
	  auto tapped = std::dynamic_pointer_cast<OrderCreate>(gui.toolManager.getOrder());
	  require(tapped &&
				  std::memcmp(tapped->getData(), dragged->getData(), dragged->getDataLength()) == 0,
			  "Tap and drag serialize equivalent construction commands");
	  noOrder();
	  gui.ghostManager.removeBuilding(tapped->posX, tapped->posY);
	  gui.touch->panelOpen = true;
	  finger(SDL_FINGERDOWN, 1, iconX, iconY);
	  finger(SDL_FINGERMOTION, 1, dropX, dropY);
	  finger(SDL_FINGERUP, 1, iconX, iconY);
	  noOrder();
	  require(gui.touch->panelOpen, "Dropping back onto the source palette cancels construction");
	  finger(SDL_FINGERDOWN, 1, iconX, iconY);
	  finger(SDL_FINGERMOTION, 1, dropX, dropY);
	  finger(SDL_FINGERDOWN, 2, dropX + 30, dropY);
	  finger(SDL_FINGERUP, 1, dropX, dropY);
	  finger(SDL_FINGERUP, 2, dropX + 30, dropY);
	  noOrder();
	  require(!gui.touch->placement && gui.touch->panelOpen,
			  "Second finger cancels palette placement without a release tap");
	  gui.localTeam->noMoreBuildingSitesCountdown = 1;
	  finger(SDL_FINGERDOWN, 1, iconX, iconY);
	  finger(SDL_FINGERMOTION, 1, dropX, dropY);
	  finger(SDL_FINGERUP, 1, dropX, dropY);
	  noOrder();
	  gui.localTeam->noMoreBuildingSitesCountdown = 0;
	  gui.clearSelection();
	  gui.touch->panelOpen = false;
	  const int type = globalContainer->buildingsTypes.getTypeNum("inn", 0, false);
	  auto *building =
		  new Building(0, 0, 2, type, gui.localTeam, &globalContainer->buildingsTypes, 1, 1);
	  gui.localTeam->myBuildings[2] = building;
	  require(building->type->maxUnitWorking > 0, "Allocation fixture must accept workers");
	  auto actionPoint = [&](int kind, int value, int side = 0)
	  {
		  for (int attempt = 0; attempt < 30; ++attempt)
		  {
			  gui.drawAll(0);
			  gfx->nextFrame();
			  const auto rows = gui.touch->buildingActions();
			  const auto found =
				  std::find_if(rows.begin(), rows.end(), [&](const auto &row)
							   { return row.kind == kind && (kind == 7 || row.value == value); });
			  require(found != rows.end(), "Building action must be available");
			  const auto r = gui.touch->panelContent();
			  const double u = gfx->logicalUnitsPerPoint();
			  const double top =
				  r.y + (std::distance(rows.begin(), found) * InGameTouchTheme::inspectorRow -
						 gui.touch->actionScroll) *
							u;
			  if (top >= r.y && top + 48 * u <= r.y + r.h)
				  return GAGCore::ViewPoint{kind == 7  ? r.x + (value + 1.5) * r.w / 3
											: side < 0 ? r.x + 24 * u
											: side > 0 ? r.x + r.w - 24 * u
													   : r.x + r.w / 2,
											top + 24 * u};
			  const float x = r.x + r.w / 2, y = r.y + r.h / 2;
			  const float delta = (top < r.y ? 1 : -1) * std::min(r.h / 3, 56 * u);
			  finger(SDL_FINGERDOWN, 1, x, y);
			  finger(SDL_FINGERMOTION, 1, x, y + delta);
			  finger(SDL_FINGERUP, 1, x, y + delta);
		  }
		  throw std::runtime_error("Building action must be reachable by scrolling");
	  };
	  auto pressAction = [&](int kind, int value = 0, int side = 0)
	  {
		  auto p = actionPoint(kind, value, side);
		  tap(p.x, p.y);
	  };
	  auto *rangeFlag =
		  new Building(0, 0, 3, globalContainer->buildingsTypes.getTypeNum("warflag", 0, false),
					   gui.localTeam, &globalContainer->buildingsTypes, 1, 1);
	  gui.localTeam->myBuildings[3] = rangeFlag;
	  require(rangeFlag->type->defaultUnitStayRange && rangeFlag->type->maxUnitWorking,
			  "Flag fixture needs range and workers");
	  gui.orderQueue.clear();
	  for (auto [width, height] : {std::pair{320, 568}, {568, 320}})
	  {
		  const int oldW = gfx->getW(), oldH = gfx->getH();
		  SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()), width, height);
		  SDL_Event resized{};
		  resized.type = SDL_WINDOWEVENT;
		  resized.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
		  GAGCore::GraphicContext::translateMouseEvent(&resized);
		  gui.viewportResized(oldW, oldH, gfx->getW(), gfx->getH());
		  const float unit = gfx->logicalUnitsPerPoint();
		  gui.setSelection(GameGUI::BUILDING_SELECTION, building);
		  // Info opens the inspector for the selected entity.
		  tap(gfx->getW() * 2.5f / 6, gfx->getH() - 24 * unit);
		  auto workerPoint = actionPoint(6, 0, 1);
		  const float plusX = workerPoint.x, rowY = workerPoint.y;
		  const auto ui = gui.touch->layout();
		  const int before = gui.displayedMaxUnitWorking(*building),
					authoritative = building->maxUnitWorking;
		  const auto simulation = gui.game.checkSum();
		  tap(plusX, rowY);
		  tap(plusX, rowY);
		  require(gui.orderQueue.size() == 2, "Two allocation taps must queue exactly two orders");
		  auto first = std::dynamic_pointer_cast<OrderModifyBuilding>(gui.orderQueue.front());
		  gui.orderQueue.pop_front();
		  auto second = std::dynamic_pointer_cast<OrderModifyBuilding>(gui.orderQueue.front());
		  gui.orderQueue.pop_front();
		  require(first && second && first->gid == building->gid &&
					  first->numberRequested == before + 1 && second->numberRequested == before + 2,
				  "Rapid allocation taps use pending values and the shared order format");
		  require(building->maxUnitWorking == authoritative && gui.game.checkSum() == simulation,
				  "Allocation UI must not change authoritative simulation state");
		  const auto track = gui.touch->panelContent();
		  const float startX = track.x + track.w / 2, endX = track.x + track.w - 60 * unit;
		  finger(SDL_FINGERDOWN, 1, startX, rowY);
		  finger(SDL_FINGERMOTION, 1, endX, rowY);
		  require(gui.touch->allocation && gui.orderQueue.empty(),
				  "Slider drag previews without intermediate orders");
		  finger(SDL_FINGERUP, 1, endX, rowY);
		  require(!gui.touch->allocation && gui.orderQueue.size() == 1,
				  "Slider release sends exactly one allocation order");
		  gui.orderQueue.clear();
		  finger(SDL_FINGERDOWN, 1, startX, rowY);
		  finger(SDL_FINGERMOTION, 1, endX, rowY);
		  finger(SDL_FINGERDOWN, 2, startX, rowY);
		  finger(SDL_FINGERUP, 1, endX, rowY);
		  finger(SDL_FINGERUP, 2, startX, rowY);
		  require(!gui.touch->allocation && gui.orderQueue.empty(),
				  "Second finger cancels slider without an order");
		  gui.drawAll(0);
		  gfx->printScreen(width < height ? "touch-allocation-portrait.bmp"
										  : "touch-allocation-landscape.bmp");
		  gfx->nextFrame();
		  gui.requestWorkerAllocation(*building, MAX_UNIT_WORKING);
		  gui.orderQueue.clear();
		  tap(plusX, rowY);
		  require(gui.orderQueue.empty(), "Allocation at maximum must not queue duplicates");
		  finger(SDL_FINGERDOWN, 1, plusX, rowY);
		  gui.clearSelection();
		  finger(SDL_FINGERUP, 1, plusX, rowY);
		  require(gui.orderQueue.empty(), "Selection changes cancel held allocation gestures");
		  gui.setSelection(GameGUI::BUILDING_SELECTION, building);
		  gui.requestWorkerAllocation(*building, 0);
		  gui.orderQueue.clear();
		  pressAction(6, 0, -1);
		  require(gui.orderQueue.empty(), "Allocation at zero must not queue duplicates");
		  for (int priority : {-1, 0, 1})
		  {
			  pressAction(7, priority);
			  require(gui.orderQueue.size() == 1, "Priority tap emits exactly one order");
			  auto order = std::dynamic_pointer_cast<OrderChangePriority>(gui.orderQueue.front());
			  gui.orderQueue.clear();
			  require(order && order->gid == building->gid && order->priority == priority,
					  "Priority uses the shared order format");
			  pressAction(7, priority);
			  require(gui.orderQueue.empty(), "Selected pending priority is a no-op");
		  }
		  require(gui.game.checkSum() == simulation,
				  "Priority changes stay outside authoritative state");
		  gui.drawAll(0);
		  gfx->printScreen(width < height ? "touch-priority-portrait.bmp"
										  : "touch-priority-landscape.bmp");
		  gfx->nextFrame();
		  gui.setSelection(GameGUI::BUILDING_SELECTION, rangeFlag);
		  const int rangeBefore = gui.displayedUnitStayRange(*rangeFlag);
		  const auto rangeChecksum = gui.game.checkSum();
		  pressAction(8, 0, 1);
		  pressAction(8, 0, 1);
		  require(gui.orderQueue.size() == 2, "Rapid range taps queue two orders");
		  for (int delta : {1, 2})
		  {
			  auto order = std::dynamic_pointer_cast<OrderModifyFlag>(gui.orderQueue.front());
			  gui.orderQueue.pop_front();
			  require(order && order->gid == rangeFlag->gid && order->range == rangeBefore + delta,
					  "Range uses pending values and shared orders");
		  }
		  gui.drawAll(0);
		  gfx->printScreen(width < height ? "touch-range-portrait.bmp"
										  : "touch-range-landscape.bmp");
		  gfx->nextFrame();
		  gui.requestFlagRange(*rangeFlag, rangeFlag->type->maxUnitStayRange);
		  gui.orderQueue.clear();
		  pressAction(8, 0, 1);
		  require(gui.orderQueue.empty(), "Maximum range is a no-op");
		  gui.requestFlagRange(*rangeFlag, 0);
		  gui.orderQueue.clear();
		  pressAction(8, 0, -1);
		  require(gui.orderQueue.empty(), "Zero range is a no-op");
		  finger(SDL_FINGERDOWN, 1, plusX, rowY);
		  gui.setSelection(GameGUI::BUILDING_SELECTION, building);
		  finger(SDL_FINGERUP, 1, plusX, rowY);
		  require(gui.orderQueue.empty(),
				  "Changing selected buildings cancels held range controls");
		  require(gui.game.checkSum() == rangeChecksum,
				  "Range controls preserve authoritative state");
		  tap(gfx->getW() * 5.5f / 6, gfx->getH() - 24 * unit);
		  require(gui.inGameMenu == GameGUI::IGM_MAIN, "Toolbar opens the in-game pause menu");
		  gui.drawAll(0);
		  gfx->printScreen(width < height ? "touch-pause-portrait.bmp"
										  : "touch-pause-landscape.bmp");
		  gfx->nextFrame();
		  const auto menuRows = gui.touch->dialogRows;
		  const auto back = std::find_if(menuRows.begin(), menuRows.end(),
										 [](const auto &row) { return row.footer; });
		  require(back != menuRows.end(), "Phone menu keeps Return fixed and reachable");
		  const float returnX = back->rect.x + back->rect.w / 2,
					  returnY = back->rect.y + back->rect.h / 2;
		  finger(SDL_FINGERDOWN, 1, returnX, returnY);
		  finger(SDL_FINGERMOTION, 1, returnX + 20 * unit, returnY);
		  finger(SDL_FINGERUP, 1, returnX + 20 * unit, returnY);
		  require(gui.inGameMenu == GameGUI::IGM_MAIN, "A dragged pause button must not activate");
		  SDL_Event mouse{};
		  mouse.type = SDL_MOUSEBUTTONDOWN;
		  mouse.button.button = SDL_BUTTON_LEFT;
		  mouse.button.x = int(returnX);
		  mouse.button.y = int(returnY);
		  gui.processEvent(&mouse);
		  mouse.type = SDL_MOUSEBUTTONUP;
		  gui.processEvent(&mouse);
		  require(!gui.inGameMenu, "Mouse activates the same visible Return control");
		  tap(gfx->getW() * 5.5f / 6, gfx->getH() - 24 * unit);
		  gui.drawAll(0);
		  finger(SDL_FINGERDOWN, 1, returnX, returnY);
		  finger(SDL_FINGERDOWN, 2, returnX, returnY);
		  finger(SDL_FINGERUP, 2, returnX, returnY);
		  finger(SDL_FINGERUP, 1, returnX, returnY);
		  require(gui.inGameMenu == GameGUI::IGM_MAIN,
				  "Switching back to touch consumes the whole gesture before accepting an action");
		  gui.drawAll(0);
		  gfx->nextFrame();
		  tap(returnX, returnY);
		  require(gui.inGameMenu == GameGUI::IGM_NONE && gui.orderQueue.empty(),
				  "Return resumes without leaking a world order");
		  tap(gfx->getW() * 2.5f / 6,
			  gfx->getH() - 24 * unit); // Close the inspector before the next orientation.
		  gui.clearSelection();
	  }

	  int workerSlot = 0;
	  while (workerSlot < Unit::MAX_COUNT && gui.localTeam->myUnits[workerSlot])
		  ++workerSlot;
	  require(workerSlot < Unit::MAX_COUNT, "Repair fixture has a free worker slot");
	  gui.localTeam->myUnits[workerSlot] = new Unit(0, 0, workerSlot, WORKER, gui.localTeam, 3);
	  bool constructionSpace = false;
	  for (int y = 0; y < gui.game.map.getH() && !constructionSpace; ++y)
		  for (int x = 0; x < gui.game.map.getW() && !constructionSpace; ++x)
		  {
			  building->posX = x;
			  building->posY = y;
			  constructionSpace = building->isHardSpaceForBuildingSite(Building::REPAIR) &&
								  building->isHardSpaceForBuildingSite(Building::UPGRADE);
		  }
	  require(constructionSpace, "Fixture has space for repair and upgrade");

	  auto fixture = [&](const char *name, int slot)
	  {
		  auto *b =
			  new Building(0, 0, slot, globalContainer->buildingsTypes.getTypeNum(name, 0, false),
						   gui.localTeam, &globalContainer->buildingsTypes, 1, 1);
		  gui.localTeam->myBuildings[slot] = b;
		  return b;
	  };
	  auto *swarm = fixture("swarm", 4);
	  auto *clearing = fixture("clearingflag", 5);
	  auto *exploring = fixture("explorationflag", 6);
	  auto *wall = fixture("stonewall", 7);
	  auto openActions = [&](Building *b)
	  {
		  gui.setSelection(GameGUI::BUILDING_SELECTION, b);
		  gui.touch->panelOpen = true;
		  gui.drawAll(0);
		  gui.touch->actionScroll = 0;
		  gui.orderQueue.clear();
	  };
	  for (auto [width, height] : {std::pair{320, 568}, {568, 320}})
	  {
		  const int oldW = gfx->getW(), oldH = gfx->getH();
		  SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()), width, height);
		  SDL_Event resized{};
		  resized.type = SDL_WINDOWEVENT;
		  resized.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
		  GAGCore::GraphicContext::translateMouseEvent(&resized);
		  gui.viewportResized(oldW, oldH, gfx->getW(), gfx->getH());
		  openActions(swarm);
		  const auto checksum = gui.game.checkSum();
		  for (int type = 0; type < NB_UNIT_TYPE; ++type)
		  {
			  const auto before = gui.displayedRatio(*swarm);
			  pressAction(0, type, 1);
			  pressAction(0, type, 1);
			  require(gui.orderQueue.size() == 2, "Rapid production taps queue exactly two orders");
			  for (int delta : {1, 2})
			  {
				  auto order = std::dynamic_pointer_cast<OrderModifySwarm>(gui.orderQueue.front());
				  gui.orderQueue.pop_front();
				  require(order && order->gid == swarm->gid,
						  "Production uses the shared order and building");
				  for (int i = 0; i < NB_UNIT_TYPE; ++i)
					  require(order->ratio[i] == before[i] + (i == type ? delta : 0),
							  "Ratio edits preserve other pending values");
			  }
			  auto values = gui.displayedRatio(*swarm);
			  values[type] = MAX_RATIO_RANGE;
			  gui.pendingFor(swarm->gid).pendingRatio = values;
			  pressAction(0, type, 1);
			  require(gui.orderQueue.empty(), "Maximum ratio tap emits no order");
			  values[type] = 0;
			  gui.pendingFor(swarm->gid).pendingRatio = values;
			  pressAction(0, type, -1);
			  require(gui.orderQueue.empty(), "Zero ratio tap emits no order");
		  }
		  require(gui.game.checkSum() == checksum, "Ratio UI does not mutate the simulation");
		  gui.drawAll(0);
		  gfx->printScreen(width < height ? "touch-actions-portrait.bmp"
										  : "touch-actions-landscape.bmp");
		  gfx->nextFrame();
		  openActions(clearing);
		  for (int resource = 0; resource < BASIC_COUNT; ++resource)
			  if (resource != STONE)
			  {
				  const bool before = gui.displayedClearingResource(*clearing, resource);
				  pressAction(1, resource);
				  pressAction(1, resource);
				  require(gui.orderQueue.size() == 2,
						  "Clearing toggles each queue exactly one order");
				  for (bool value : {!before, before})
				  {
					  auto order = std::dynamic_pointer_cast<OrderModifyClearingFlag>(
						  gui.orderQueue.front());
					  gui.orderQueue.pop_front();
					  require(order && order->gid == clearing->gid &&
								  order->clearingResources[resource] == value,
							  "Clearing toggle uses pending state");
				  }
			  }
		  for (auto *flag : {rangeFlag, exploring})
		  {
			  openActions(flag);
			  const int count = flag == rangeFlag ? NB_UNIT_LEVELS : EXPLORATION_FLAG_OPTION_COUNT;
			  for (int level = 0; level < count; ++level)
			  {
				  const int previous = gui.displayedMinLevelToFlag(*flag);
				  pressAction(2, level);
				  require(gui.orderQueue.size() == size_t(previous != level),
						  "Requirement changes suppress no-ops");
				  if (previous != level)
				  {
					  auto order = std::dynamic_pointer_cast<OrderModifyMinLevelToFlag>(
						  gui.orderQueue.front());
					  gui.orderQueue.clear();
					  require(order && order->gid == flag->gid && order->minLevelToFlag == level,
							  "Flag requirement preserves shared order format");
				  }
			  }
		  }
		  openActions(wall);
		  require(gui.touch->allocationRect().h == 48 * gfx->logicalUnitsPerPoint(),
				  "Unified inspector retains its identity header");
		  pressAction(4);
		  require(gui.orderQueue.empty() && gui.touch->confirmDestroy,
				  "Destroy first enters confirmation");
		  pressAction(5);
		  require(gui.orderQueue.empty() && !gui.touch->confirmDestroy,
				  "Destruction can be canceled");
		  pressAction(4);
		  pressAction(4);
		  require(gui.orderQueue.size() == 1 &&
					  std::dynamic_pointer_cast<OrderDelete>(gui.orderQueue.front()),
				  "Confirmed destruction emits one shared order");
		  gui.orderQueue.clear();
		  auto p = actionPoint(4, 0);
		  finger(SDL_FINGERDOWN, 1, p.x, p.y);
		  wall->buildingState = Building::WAITING_FOR_DESTRUCTION;
		  finger(SDL_FINGERUP, 1, p.x, p.y);
		  require(gui.orderQueue.empty(), "A state transition cancels the held action");
		  pressAction(4);
		  require(gui.orderQueue.size() == 1 &&
					  std::dynamic_pointer_cast<OrderCancelDelete>(gui.orderQueue.front()),
				  "Pending destruction can be canceled by touch");
		  gui.orderQueue.clear();
		  wall->buildingState = Building::ALIVE;
		  openActions(building);
		  for (bool repair : {true, false})
		  {
			  building->hp = building->type->hpMax - (repair ? 1 : 0);
			  const auto stateBefore = gui.game.checkSum();
			  pressAction(3);
			  require(gui.orderQueue.size() == 1, "Repair/upgrade starts with exactly one order");
			  auto order = std::dynamic_pointer_cast<OrderConstruction>(gui.orderQueue.front());
			  require(order && order->gid == building->gid,
					  "Repair/upgrade uses shared construction order");
			  gui.orderQueue.clear();
			  require(gui.game.checkSum() == stateBefore,
					  "Construction requests do not mutate simulation");
		  }
		  building->hp = building->type->hpMax - 1;
		  auto repairPoint = actionPoint(3, 0);
		  finger(SDL_FINGERDOWN, 1, repairPoint.x, repairPoint.y);
		  building->hp = building->type->hpMax;
		  finger(SDL_FINGERUP, 1, repairPoint.x, repairPoint.y);
		  require(gui.orderQueue.empty(), "Healing must not turn a held Repair into Upgrade");
		  for (auto state : {Building::REPAIR, Building::UPGRADE})
		  {
			  building->constructionResultState = state;
			  pressAction(3);
			  require(
				  gui.orderQueue.size() == 1 &&
					  std::dynamic_pointer_cast<OrderCancelConstruction>(gui.orderQueue.front()),
				  "Construction cancellation uses shared order");
			  gui.orderQueue.clear();
		  }
		  building->constructionResultState = Building::NO_CONSTRUCTION;
		  gui.clearSelection();
		  gui.touch->panelOpen = false;
	  }

	  for (const auto *key : {"[Actions]", "[Info]", "[Minimap]", "[Fast forward]",
							  "[Hide keyboard]", "[shutdown save failed]", "[menu]",
							  "[Editor tools]", "[Editor map]", "[Pan map]", "[Edit map]"})
		  require(!GAGCore::Toolkit::getStringTable()->getString(key).empty(),
				  "New interface translations must not be blank");

	  auto tr = [](const char *key)
	  { return std::string(GAGCore::Toolkit::getStringTable()->getString(key)); };
	  auto pressDialog = [&](const std::string &text)
	  {
		  for (int attempt = 0; attempt < 40; ++attempt)
		  {
			  gui.drawAll(0);
			  gfx->nextFrame();
			  auto rows = gui.touch->dialogRows;
			  const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto &row)
											  { return row.text == text && row.kind; });
			  require(found != rows.end(), ("Missing phone dialog action: " + text).c_str());
			  const auto r = found->rect, content = gui.touch->dialogContent;
			  if (found->footer || (r.y >= content.y && r.y + r.h <= content.y + content.h))
			  {
				  tap(r.x + r.w / 2, r.y + r.h / 2);
				  return;
			  }
			  const float x = content.x + content.w / 2, y = content.y + content.h / 2;
			  const float move = (r.y < content.y ? 1 : -1) *
								 std::min(content.h / 3, 80.0 * gfx->logicalUnitsPerPoint());
			  finger(SDL_FINGERDOWN, 1, x, y);
			  finger(SDL_FINGERMOTION, 1, x, y + move);
			  finger(SDL_FINGERUP, 1, x, y + move);
		  }
		  require(false, "Dialog action must be reachable by scrolling");
	  };
	  for (auto [width, height] : {std::pair{320, 568}, {568, 320}})
	  {
		  SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()), width, height);
		  SDL_Event resized{};
		  resized.type = SDL_WINDOWEVENT;
		  resized.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
		  GAGCore::GraphicContext::translateMouseEvent(&resized);
		  gui.viewportResized(800, 600, gfx->getW(), gfx->getH());
		  gui.inGameMenu = GameGUI::IGM_MAIN;
		  gui.gameMenuScreen = std::make_unique<InGameMainScreen>();
		  pressDialog(tr("[Options]"));
		  require(gui.inGameMenu == GameGUI::IGM_OPTION, "Phone menu opens options");
		  pressDialog(tr("[Dialog text size]") + ": 150%");
		  require(globalContainer->settings.mobileDialogTextPercent == 150,
				  "Text size changes without leaving menu");
		  pressDialog(tr("[Dialog text size]") + ": 100%");
		  gui.drawAll(0);
		  gfx->printScreen(width < height ? "touch-options-portrait.bmp"
										  : "touch-options-landscape.bmp");
		  gfx->nextFrame();
		  pressDialog(tr("[Mute]"));
		  require(!globalContainer->settings.mute, "Large mute row updates shared options");
		  pressDialog(tr("[ok]"));
		  require(!gui.inGameMenu, "Options footer remains reachable");
		  globalContainer->settings.mute = true;
		  gui.inGameMenu = GameGUI::IGM_OBJECTIVES;
		  gui.gameMenuScreen = std::make_unique<InGameObjectivesScreen>(&gui, false);
		  pressDialog(tr("[hints]"));
		  gui.drawAll(0);
		  gfx->printScreen(width < height ? "touch-objectives-portrait.bmp"
										  : "touch-objectives-landscape.bmp");
		  gfx->nextFrame();
		  pressDialog(tr("[ok]"));
		  require(!gui.inGameMenu, "Objectives tabs and footer work by touch");
		  gui.inGameMenu = GameGUI::IGM_SAVE;
		  gui.gameMenuScreen =
			  std::make_unique<LoadSaveScreen>("games", "game", false, tr("[save game]"), "Phone",
											   glob2FilenameToName, glob2NameToFilename);
		  pressDialog("Phone");
		  SDL_Event text{};
		  text.type = SDL_TEXTINPUT;
		  std::strcpy(text.text.text, " test");
		  gui.processEvent(&text);
		  require(std::string(static_cast<LoadSaveScreen *>(gui.gameMenuScreen.get())->getName()) ==
					  "Phone test",
				  "Phone filename uses real text input");
		  pressDialog(tr("[Hide keyboard]"));
		  gui.drawAll(0);
		  gfx->printScreen(width < height ? "touch-save-portrait.bmp" : "touch-save-landscape.bmp");
		  gfx->nextFrame();
		  pressDialog(tr("[Cancel]"));
		  require(!gui.inGameMenu, "Save cancellation is always reachable");
		  gui.inGameMenu = GameGUI::IGM_MAIN;
		  gui.gameMenuScreen = std::make_unique<InGameMainScreen>();
		  gui.touch->prepareDialog();
		  require(std::none_of(gui.touch->dialogRows.begin(), gui.touch->dialogRows.end(),
							   [](const auto &row) { return row.kind == 100 && row.index == 1; }),
				  "Chat is not mixed into the game menu");
		  gui.touch->menuAction(1);
		  require(gui.typingInputScreen, "Tactical chat action opens the composer");
		  SDL_Event composing{};
		  composing.type = SDL_TEXTEDITING;
		  std::strcpy(composing.edit.text, "ni");
		  gui.processEvent(&composing);
		  SDL_Event commitKey{};
		  commitKey.type = SDL_KEYDOWN;
		  commitKey.key.keysym.sym = SDLK_RETURN;
		  gui.processEvent(&commitKey);
		  require(gui.typingInputScreen && gui.typingInputScreen->getText().empty() &&
					  gui.orderQueue.empty(),
				  "IME candidate confirmation must not send chat or mutate the committed draft");
		  text = {};
		  text.type = SDL_TEXTINPUT;
		  std::strcpy(text.text.text, "Hello");
		  gui.processEvent(&text);
		  gui.orderQueue.clear();
		  pressDialog("Send");
		  require(!gui.typingInputScreen && gui.orderQueue.size() == 1 &&
					  std::dynamic_pointer_cast<MessageOrder>(gui.orderQueue.front()),
				  "Chat sends once through shared orders");
		  gui.orderQueue.clear();
	  }

	  {
		  GAGGUI::ScreenStack stack(*gfx);
		  auto results = std::make_unique<EndGameScreen>(&gui);
		  auto *view = results.get();
		  stack.push(std::move(results));
		  stack.frame(SDL_GetTicks(), {});
		  require(!view->resultControls.empty(), "Results exposes the shared chart controls");
		  view->activateResultControl(100);
		  require(view->metricPicker.isOpen(), "Metric selector opens an explicit dropdown");
		  SDL_Event key{};
		  key.type = SDL_KEYDOWN;
		  key.key.keysym.sym = SDLK_END;
		  view->handleExecutionEvent(key);
		  key.key.keysym.sym = SDLK_RETURN;
		  view->handleExecutionEvent(key);
		  require(view->selectedMetric == 35 && !view->metricPicker.isOpen(),
				  "Every metric is selectable without cycling pages");
		  if (!view->teams.empty())
		  {
			  const int team = view->teams.front().teamNum;
			  view->activateResultControl(200);
			  view->selectMetric(0);
			  for (size_t i = 0; i < view->teams.size(); ++i)
				  if (view->teams[i].teamNum == team)
					  require(!view->teams[i].enabled,
							  "Metric changes preserve team filtering across sorting");
		  }
		  view->activateResultControl(101);
		  require(view->expandedChart, "Chart expansion is available");
		  stack.frame(SDL_GetTicks(), {});
		  view->endExecute(0);
		  stack.frame(SDL_GetTicks(), {});
	  }

	  globalContainer->replayReader = std::make_unique<ReplayReader>();
	  require(globalContainer->replayReader->loadReplay("replays/touch-preview.replay"),
			  "Replay reader loads");
	  globalContainer->replaying = true;
	  globalContainer->replayVisibleTeams = 0xffffffff;
	  auto replayMap = Engine::loadMapHeader("replays/touch-preview.replay");
	  require(gui.loadFromHeaders(replayMap, players, true, true, false,
								  "replays/touch-preview.replay"),
			  "Replay world loads");
	  gui.localTeamNo = 0;
	  gui.localPlayer = 0;
	  gui.adjustLocalTeam();
	  for (auto [width, height] : {std::pair{320, 568}, {568, 320}})
	  {
		  SDL_SetWindowSize(SDL_GetWindowFromID(gfx->windowID()), width, height);
		  SDL_Event resized{};
		  resized.type = SDL_WINDOWEVENT;
		  resized.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
		  GAGCore::GraphicContext::translateMouseEvent(&resized);
		  gui.viewportResized(800, 600, gfx->getW(), gfx->getH());
		  gui.drawAll(0);
		  gfx->nextFrame();
		  const auto before = gui.game.checkSum();
		  const double unit = gfx->logicalUnitsPerPoint(), y = gfx->getH() - 24 * unit;
		  gui.gamePaused = false;
		  globalContainer->replayFastForward = false;
		  gui.orderQueue.clear();
		  tap(gfx->getW() / 12.0, y);
		  require(gui.gamePaused, "Replay toolbar pauses");
		  tap(gfx->getW() / 4.0, y);
		  require(!gui.gamePaused && globalContainer->replayFastForward,
				  "Replay speed resumes fast playback");
		  tap(gfx->getW() / 4.0, y);
		  require(!globalContainer->replayFastForward, "Replay speed returns to normal");
		  gui.touch->menuAction(30);
		  require(globalContainer->replayFastForward,
				  "Replay menu speed uses shared playback state");
		  require(gui.orderQueue.empty() && !gui.toolManager.getOrder() &&
					  gui.game.checkSum() == before,
				  "Replay controls issue no simulation orders");
		  if (gui.inGameMenu)
		  {
			  gui.inGameMenu = GameGUI::IGM_NONE;
			  gui.gameMenuScreen.reset();
		  }
	  }
	  globalContainer->replaying = false;
	  globalContainer->replayReader.reset();
	  editorInteractions();
      editorFileInteractions();
      editorAreaNameInteractions();
  }
};
int main()
{
    if(!SDL_getenv("GLOB2_USER_DATA_DIR")) return 2;
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    // Exercise the legacy mouse sidebar first, even on touch-capable hosts;
    // run() explicitly switches to the phone presentation for the touch cases.
    SDL_setenv("GLOB2_MOBILE_UI","0",1);
    try {
        globalContainer=new GlobalContainer("glob2-touch-test");
        globalContainer->settings.screenWidth=800;globalContainer->settings.screenHeight=600;
        globalContainer->settings.screenFlags=GAGCore::GraphicContext::PORTABLEGPU;
        globalContainer->settings.mute=true;globalContainer->load();
        require(SDLNet_Init()==0,"SDL networking init failed");
        GameGUITouchHarness::run();
        delete globalContainer;globalContainer=nullptr;SDLNet_Quit();
        std::puts("PASS: actual gameplay touch, toroidal pan, preview, confirmation, validation, cancellation and duplicate suppression");
        return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
