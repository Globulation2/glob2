// SPDX-License-Identifier: GPL-3.0-or-later
// Generate the bundled colony and exercise the real simulation/presentation.
#include "GlobalContainer.h"
#include "FrontendTheme.h"
#include "MenuColony.h"
#include <StringTable.h>
#include "MainMenuScreen.h"
#include "SettingsScreen.h"
#include "LANMenuScreen.h"
#include "ChooseMapScreen.h"
#include "CustomGameScreen.h"
#include "CustomGameOtherOptions.h"
#include "GameGUILoadSave.h"
#include "CampaignMainMenu.h"
#include "EditorMainMenu.h"
#include "CreditScreen.h"
#include "MapGenerationDescriptor.h"
#include "FertilityCalculator.h"
#include "Order.h"
#include "Player.h"
#include "Unit.h"
#include "GameGUI.h"
#include "Engine.h"
#include "MapEdit.h"
#include "MapEditorScreen.h"
#include "EndGameScreen.h"
#include "CampaignMenuScreen.h"
#include "CampaignSelectorScreen.h"
#include "NewMapScreen.h"
#include "LANFindScreen.h"
#include "YOGLoginScreen.h"
#include "YOGRegisterScreen.h"
#include "YOGClient.h"
#include "ReplayWriter.h"
#include "DatasetWriter.h"
#include <GUIButton.h>
#include <GUITextInput.h>
#include <GUIText.h>
#include <GUIList.h>
#include <filesystem>
#include <sstream>
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>
#include <ScreenStack.h>
#include <SDL_image.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <chrono>

GlobalContainer* globalContainer=nullptr;
using namespace GAGCore;
std::string replayFilenameToName(const std::string&);

void require(bool condition, const char* message)
{
	if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(2); }
}
// Exercise presentation through the real dispatch path, and inspect raster
// output rather than trusting widget bounds to describe wrapped text.
void checkMenuPainting()
{
	class CountingSurface : public DrawableSurface
	{
	public:
		CountingSurface() : DrawableSurface(240, 100) {}
		int presentations = 0;
		void nextFrame() override { ++presentations; DrawableSurface::nextFrame(); }
	} surface;
	class TestScreen : public GAGGUI::Screen
	{
	public:
		explicit TestScreen(DrawableSurface* target) { gfx = target; }
		void paint() override {}
		void onAction(GAGGUI::Widget*, GAGGUI::Action, int, int) override {}
	} screen(&surface);
	FrontendScope scope;
	screen.dispatchPaint(false);
	require(surface.presentations == 0, "modal background is not presented separately");
	surface.nextFrame();
	require(surface.presentations == 1, "composed modal frame presents once");
	screen.dispatchPaint();
	require(surface.presentations == 2, "ordinary screen still presents");

	auto* label = new GAGGUI::Text(10, 10, ALIGN_LEFT, ALIGN_TOP,
		"standard", "A very long map name with enough words to overflow several rows", 80, 35);
	screen.addWidget(label);
	screen.dispatchInit();
	for (bool wrap : {false, true})
	{
		surface.setClipRect();
		surface.drawFilledRect(0, 0, 240, 100, Color(255, 0, 255));
		label->setWordWrap(wrap);
		label->paint();
		auto* raster = surface.getSDLSurface();
		const Uint32 background = SDL_MapRGBA(raster->format, 255, 0, 255, 255);
		int ink = 0;
		for (int y = 0; y < 100; ++y)
			for (int x = 0; x < 240; ++x)
			{
				const auto pixel = reinterpret_cast<Uint32*>(static_cast<Uint8*>(raster->pixels) + y * raster->pitch)[x];
				if (pixel == background) continue;
				++ink;
				require(x >= 10 && x < 90 && y >= 10 && y < 45, "text stays within its allocated box");
				if (!wrap) require(y < 10 + Toolkit::getFont("standard")->getStringHeight(label->getText()), "single-line labels do not wrap");
			}
		require(ink > 0, "bounded labels remain visible");
		int x, y, w, h;
		surface.getClipRect(&x, &y, &w, &h);
		require(x == 0 && y == 0 && w == 240 && h == 100, "text restores clipping for subsequent widgets");
	}
}

void generate(const char* path)
{
	setSyncRandSeed(481516);
	std::srand(481516);
	Game game(nullptr);
	MapGenerationDescriptor d;
	d.method=MapGenerationDescriptor::eOLDISLANDS;
	d.oldIslandSize=35;
	d.wDec=d.hDec=7;
	d.nbTeams=1; d.nbWorkers=48;
	d.waterRatio=25; d.grassRatio=65; d.sandRatio=10;
	game.map.setSize(d.wDec,d.hDec);
	game.map.setGame(&game);
	require(game.map.oldMakeIslandsMap(d) && game.oldMakeIslandsMap(d),"generate terrain");
	// The legacy island generator supplies terrain only. Seed small groves
	// and grain fields with the existing resource API, leaving walking lanes.
	const int bx=d.bootX[0], by=d.bootY[0];
	for(int y=0;y<game.map.getH();++y) for(int x=0;x<game.map.getW();++x)
	{
		const int dx=((x-bx+64)&127)-64, dy=((y-by+64)&127)-64;
		if(game.map.getUMTerrain(x,y)!=GRASS || (std::abs(dx)<6 && std::abs(dy)<6)) continue;
		if((x%8<4) && (y%8<4))
		{
			const int resource=((x/8+y/8)%5==0) ? STONE : ((x/8+y/8)%2 ? CORN : WOOD);
			game.map.setResource(x,y,resource,3);
		}
	}
	game.sgslScript.compileScript(&game);
	FertilityCalculator::compute(game.map, {});
	GameHeader header;
	header.setNumberOfPlayers(1);
	header.setRandomSeed(481516);
	header.getBasePlayer(0)=BasePlayer(0,"Menu colony",0,BasePlayer::playerTypeFromImplementationID(AI::ECONO));
	header.getWinningConditions().clear();
	game.setGameHeader(header);
	game.setAlliances();
	game.map.getResourceGradient(0,CORN,0);
	game.teams[0]->color=Color(73,191,184);
	for (int i=0;i<12000;++i)
	{
		auto order=game.players[0]->ai->getOrder(false);
		order->sender=0;
		game.executeOrder(order,0);
		game.syncStep(0);
		if(i%3000==0) {
			int buildings=0, units=0;
			for(int j=0;j<Building::MAX_COUNT;++j) { auto* b=game.teams[0]->myBuildings[j]; if(b && !b->type->isVirtual) ++buildings; }
			for(int j=0;j<Unit::MAX_COUNT;++j) if(game.teams[0]->myUnits[j]) ++units;
			std::cout << "warmup_tick=" << i << " buildings=" << buildings << " units=" << units << std::endl;
		}
	}
	BinaryOutputStream out(Toolkit::getFileManager()->openOutputStreamBackend(path));
	out.writeText("glob2-menu-colony-1","format");
	game.save(&out,false,"Menu colony");
	out.writeText(getSyncRandState(),"rng");
	std::cout << "Generated colony at tick " << game.stepCounter << '\n';
}

template<class T> class Preview : public T
{
public:
	using T::T;
	void prepare() { if(!prepared) { this->gfx=globalContainer->gfx; this->dispatchInit(); prepared=true; } }
	void render() { prepare(); this->paint(); for(auto* w:this->widgets) if(w->visible) w->paint(); }
	void advance(unsigned frames) { prepare(); for(unsigned i=0;i<frames;++i) this->dispatchTimer(i*40); }
	void checkBounds()
	{
		prepare();
		for(auto* widget:this->widgets) if(widget->visible)
			if(auto* button=dynamic_cast<GAGGUI::Button*>(widget))
			{
				const auto r=button->getScreenRect();
				require(r.x>=0 && r.y>=0 && r.x+r.w<=this->getW() && r.y+r.h<=this->getH(),"button within screen");
			}
	}
	void selectFirstListItem()
	{
		prepare();
		for(auto* widget:this->widgets) if(widget->visible)
			if(auto* list=dynamic_cast<GAGGUI::List*>(widget))
			{
				if(list->getCount()) { list->setSelectionIndex(0); list->selectionChanged(); return; }
			}
	}
	void clickButton(int code)
	{
		prepare();
		for(auto* widget:this->widgets) if(widget->visible)
			if(auto* button=dynamic_cast<GAGGUI::Button*>(widget); button && button->returnCode==code)
			{
				const auto r=button->getScreenRect();
				SDL_Event e{}; e.button.button=SDL_BUTTON_LEFT; e.button.x=r.x+r.w/2; e.button.y=r.y+r.h/2;
				e.type=SDL_MOUSEBUTTONDOWN; this->dispatchEvents(&e);
				e.type=SDL_MOUSEBUTTONUP; this->dispatchEvents(&e); return;
			}
		require(false,"button exists");
	}
	void executeCancellation()
	{
		prepare();
		GAGGUI::Button* cancel=nullptr;
		SDL_Rect last{};
		for(auto* widget:this->widgets) if(widget->visible)
			if(auto* button=dynamic_cast<GAGGUI::Button*>(widget))
			{
				const auto r=button->getScreenRect();
				if(!cancel || r.y>last.y || (r.y==last.y && r.x>last.x)) { cancel=button; last=r; }
			}
		require(cancel,"cancellation button exists");
		SDL_Event event{}; event.type=SDL_MOUSEBUTTONDOWN; event.button.button=SDL_BUTTON_LEFT;
		event.button.x=last.x+last.w/2; event.button.y=last.y+last.h/2;
		SDL_PushEvent(&event); event.type=SDL_MOUSEBUTTONUP; SDL_PushEvent(&event);
		auto* before=GAGGUI::Style::style;
		this->execute(globalContainer->gfx,40);
		require(GAGGUI::Style::style==before,"screen execution restores theme");
	}
	void executeKeyboardCancellation()
	{
		prepare();
		SDL_Event event{}; event.type=SDL_KEYDOWN; event.key.keysym.sym=SDLK_ESCAPE;
		SDL_PushEvent(&event);
		auto* before=GAGGUI::Style::style;
		const int code=this->execute(globalContainer->gfx,40);
		require(code==T::CANCEL,"keyboard cancellation returns to menu");
		require(GAGGUI::Style::style==before,"screen execution restores theme");
	}
	int result() const { return GAGGUI::Screen::returnCode; }
	void executeEscape()
	{
		prepare();
		SDL_Event event{};event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_ESCAPE;
		SDL_PushEvent(&event);
		auto* before=GAGGUI::Style::style;
		this->execute(globalContainer->gfx,40);
		require(GAGGUI::Style::style==before,"screen execution restores theme");
	}
private:
	bool prepared=false;
};
void capture(const std::string& name,const std::string& path)
{
	FrontendScope scope;
	GAGGUI::ScreenStack screens(*globalContainer->gfx);
	if(name=="colony") { FrontendTheme::current->colony->draw(globalContainer->gfx->getW(),globalContainer->gfx->getH()); }
	else if(name=="main" || name=="fallback") { Preview<MainMenuScreen> s; s.render(); s.checkBounds(); }
	else if(name=="options") { Preview<CustomGameScreen> parent(screens); parent.selectFirstListItem(); Preview<CustomGameOtherOptions> s(parent.getGameHeader(),parent.getMapHeader(),false); s.render(); s.checkBounds(); }
	else if(name=="save-replay") { Preview<CampaignMainMenu> parent(screens); parent.render(); LoadSaveScreen s("replays","replay",false,"Save replay","",replayFilenameToName,glob2NameToFilename); s.dispatchPaint(); globalContainer->gfx->drawSurface(s.decX,s.decY,s.getSurface()); }
	else if(name=="settings" || name=="settings-buildings" || name=="settings-keys")
	{
		Preview<SettingsScreen> s;
		if(name=="settings-buildings") s.selectCategory(SettingsScreen::Category::Buildings);
		if(name=="settings-keys") s.selectCategory(SettingsScreen::Category::Controls);
		s.render(); s.checkBounds();
	}
	else if(name=="lan") { Preview<LANMenuScreen> s(screens); s.render(); s.checkBounds(); }
	else if(name=="campaign") { Preview<CampaignMainMenu> s(screens); s.render(); s.checkBounds(); }
	else if(name=="editor") { Preview<EditorMainMenu> s(screens); s.render(); s.checkBounds(); }
	else if(name=="credits") { Preview<CreditScreen> s; s.advance(450); s.render(); s.checkBounds(); }
	else if(name=="load") { Preview<ChooseMapScreen> s("games","game",true); s.render(); s.checkBounds(); }
	else if(name=="missions") { Preview<CampaignMenuScreen> s("campaigns/Tutorial_Campaign.txt",screens); s.render(); s.checkBounds(); }
	else if(name=="campaign-select") { Preview<CampaignSelectorScreen> s; s.render(); s.checkBounds(); }
	else if(name=="new-map") { Preview<NewMapScreen> s; s.render(); s.checkBounds(); }
	else if(name=="lan-find") { Preview<LANFindScreen> s(screens); s.render(); s.checkBounds(); }
	else if(name=="login") { Preview<YOGLoginScreen> s(screens,std::make_shared<YOGClient>()); s.render(); s.checkBounds(); }
	else if(name=="register") { Preview<YOGRegisterScreen> s(std::make_shared<YOGClient>()); s.render(); s.checkBounds(); }
	else if(name=="results")
	{
		GameGUI gui;
		BinaryInputStream in(Toolkit::getFileManager()->openInputStreamBackend("data/menu/colony.bin"));
		in.readText("format"); require(gui.game.load(&in),"load results fixture");
		gui.localTeamNo=0; gui.localPlayer=0; gui.adjustLocalTeam();
		Preview<EndGameScreen> s(&gui); s.render(); s.checkBounds();
	}
	else if(name=="custom" || name=="custom-players" || name=="custom-rules")
	{
		Preview<CustomGameScreen> s(screens); s.prepare();
		if(name=="custom-players") s.activateGroup(1);
		if(name=="custom-rules") s.activateGroup(2);
		s.render(); s.checkBounds();
	}
	else require(false,"unknown screen");
	DrawableSurface shot(globalContainer->gfx->getW(),globalContainer->gfx->getH());
	shot.drawSurface(0,0,globalContainer->gfx);
	require(IMG_SavePNG(shot.getSDLSurface(),path.c_str())==0,"save PNG");
}
// Only the test driver uses a timer: inject normal UI events into session loops.
struct SessionExit { int phase=0, x=0, y=0; bool replay=false; };
Uint32 exitSession(Uint32,void* data)
{
	auto& state=*static_cast<SessionExit*>(data);
	SDL_Event e{};
	if(state.replay) { e.type=SDL_KEYDOWN; e.key.keysym.sym=SDLK_RETURN; SDL_PushEvent(&e); }
	else if(state.phase==0) { e.type=SDL_KEYDOWN; e.key.keysym.sym=SDLK_ESCAPE; SDL_PushEvent(&e); }
	else if(state.phase==1)
	{
		e.type=SDL_MOUSEBUTTONDOWN; e.button.button=SDL_BUTTON_LEFT; e.button.x=state.x; e.button.y=state.y;
		SDL_PushEvent(&e); e.type=SDL_MOUSEBUTTONUP; SDL_PushEvent(&e);
	}
	else { e.type=SDL_KEYDOWN; e.key.keysym.sym=SDLK_RETURN; SDL_PushEvent(&e); }
	++state.phase;
	return 500;
}

int main(int argc,char** argv)
{
	require(argc>=3,"usage: MenuColonyHarness generate PATH | check PATH | capture SCREEN OUTPUT [W H] | soak SECONDS");
	const auto startupBegin=std::chrono::steady_clock::now();
	GlobalContainer globals("glob2-frontend-test"); globalContainer=&globals;
	globals.settings.mute=1;
	globals.settings.screenWidth=argc>4?std::atoi(argv[4]):1152;
	globals.settings.screenHeight=argc>5?std::atoi(argv[5]):720;
	globals.settings.screenFlags=std::getenv("GLOB2_PREVIEW_GL")?GraphicContext::USEGPU:0;
	globals.load();
	if (const char* lang=std::getenv("GLOB2_PREVIEW_LANGUAGE")) Toolkit::getStringTable()->setLang(Toolkit::getStringTable()->getLangCode(lang));
	const std::string mode=argv[1];
	if(mode=="generate") { generate(argv[2]); return 0; }
std::cout << "global_assets_ms=" << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-startupBegin).count() << std::endl;
	const auto themeBegin=std::chrono::steady_clock::now();
	FrontendTheme theme;
	std::cout << "theme_setup_ms=" << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-themeBegin).count() << std::endl;
	if(mode=="capture")
	{
		require(argc>=4,"capture needs screen and output");
		if(std::string(argv[2])!="fallback") require(theme.colony->load(),"load bundled colony");
		capture(argv[2],argv[3]);
		return 0;
	}
	if(mode=="check")
	{
		checkMenuPainting();
		// Team tinting must preserve transparent sprite pixels in software mode.
		DrawableSurface alpha(2,2);
		auto* pixels=static_cast<Uint32*>(alpha.getSDLSurface()->pixels);
		pixels[0]=SDL_MapRGBA(alpha.getSDLSurface()->format,51,255,153,0);
		alpha.shiftHSV(35,0,0);
		Uint8 red,green,blue,opacity; SDL_GetRGBA(pixels[0],alpha.getSDLSurface()->format,&red,&green,&blue,&opacity);
		require(opacity==0,"team tint preserves transparency");
		const auto rng=getSyncRandState();
		const auto loadBegin=std::chrono::steady_clock::now();
		require(theme.colony->load(argv[2]),"load colony");
		require(getSyncRandState()==rng,"load restores RNG");
		std::cout << "colony_load_ms=" << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-loadBegin).count() << std::endl;
		const bool replaying=globals.replaying, flags=globals.replayShowFlags;
		const auto teams=globals.replayVisibleTeams;
		theme.colony->draw(globals.gfx->getW(),globals.gfx->getH());
		require(globals.replaying==replaying && globals.replayShowFlags==flags && globals.replayVisibleTeams==teams && getSyncRandState()==rng,"drawing restores global state");
		const auto initialChecksum=theme.colony->checksum();
		const auto before=theme.colony->tick();
		theme.colony->update(1000); theme.colony->update(1040);
		require(theme.colony->tick()==before+1,"one normal tick");
		require(getSyncRandState()==rng,"step restores RNG");
		theme.colony->pause(); theme.colony->update(999999);
		require(theme.colony->tick()==before+1,"resume has no catchup");
		theme.colony->update(1999999);
		require(theme.colony->tick()==before+3,"catchup bounded to two ticks");
		const auto font=globals.standardFont->getStyle();
		auto* style=GAGGUI::Style::style;
		{
			FrontendScope menu;
			require(GAGGUI::Style::style==&theme,"menu theme");
			{ FrontendScope gameplay(false); require(GAGGUI::Style::style==style,"gameplay style");
			  FrontendScope nested; require(GAGGUI::Style::style==style,"gameplay dialog style"); }
			require(GAGGUI::Style::style==&theme,"nested restoration");
		}
		require(GAGGUI::Style::style==style && globals.standardFont->getStyle().color==font.color,"theme/font restoration");
		MenuColony a,b;
		require(a.load(argv[2]) && b.load(argv[2]),"two sessions load");
		for(int i=0;i<300;++i) { a.update(1000+i*40); theme.colony->update(2000000+i*40); b.update(1000+i*40); }
		require(a.checksum()==b.checksum(),"interleaved colony cannot change another game's result");
		require(getSyncRandState()==rng,"interleaved games restore RNG");
		require(theme.colony->checksum()!=initialChecksum,"live game state changes");
		// Compare actual GUI-backed game sessions, with and without menu work.
		auto simulate=[&](bool background)
		{
			GameGUI gui;
			BinaryInputStream in(Toolkit::getFileManager()->openInputStreamBackend(argv[2]));
			in.readText("format"); require(gui.game.load(&in),"load real-game fixture");
			std::istringstream state(in.readText("rng")+" "); state >> randomGenerator;
			gui.game.map.getResourceGradient(0,CORN,0);
			gui.localTeamNo=0; gui.localPlayer=0; gui.adjustLocalTeam();
			globals.replayWriter=std::make_unique<ReplayWriter>();
			globals.replayWriter->init("",gui);
			for(int i=0;i<300;++i)
			{
				if(background)
				{
					auto* writer=globals.replayWriter.get();
					const auto position=writer->getBuffer()->getPosition();
					theme.colony->update(3000000+i*40);
					require(globals.replayWriter.get()==writer && writer->getBuffer()->getPosition()==position,"menu cannot record into match replay");
				}
				auto order=gui.game.players[0]->ai->getOrder(false); order->sender=0;
				gui.game.executeOrder(order,0); gui.game.syncStep(0);
			}
			const auto result=std::make_pair(gui.game.checkSum(nullptr,nullptr,nullptr),getSyncRandState());
			globals.replayWriter.reset();
			return result;
		};
		require(simulate(false)==simulate(true),"real match checksum and RNG unaffected by menu");
		const auto datasetPath=(std::filesystem::temp_directory_path()/"glob2-menu-isolation.gds").string();
		globals.datasetWriter=std::make_unique<DatasetWriter>();
		require(globals.datasetWriter->open(datasetPath),"open recording isolation fixture");
		auto* dataset=globals.datasetWriter.get();
		theme.colony->pause();
		for(int i=0;i<30;++i) theme.colony->update(4000000+i*40);
		require(globals.datasetWriter.get()==dataset,"restore dataset writer");
		globals.datasetWriter.reset();
		require(std::filesystem::file_size(datasetPath)==8,"menu cannot append training records");
		std::filesystem::remove(datasetPath);
		MenuColony missing;
		require(!missing.load("data/menu/does-not-exist.bin"),"missing asset fallback");
		const auto invalidPath=(std::filesystem::temp_directory_path()/"glob2-invalid-colony.bin").string();
		{ BinaryOutputStream invalid(Toolkit::getFileManager()->openOutputStreamBackend(invalidPath)); invalid.writeText("unsupported-version","format"); }
		require(!missing.load(invalidPath),"incompatible asset fallback");
		std::filesystem::remove(invalidPath);
		{
			FrontendScope scope;
			Preview<MainMenuScreen> main; main.render(); main.checkBounds();
			const int actions[]={MainMenuScreen::CUSTOM,MainMenuScreen::CAMPAIGN,MainMenuScreen::LOAD_GAME,MainMenuScreen::TUTORIAL,MainMenuScreen::MULTIPLAYERS_YOG,MainMenuScreen::MULTIPLAYERS_LAN,MainMenuScreen::GAME_SETUP,MainMenuScreen::EDITOR,MainMenuScreen::CREDITS,MainMenuScreen::QUIT};
			SDL_Event e{}; e.type=SDL_KEYDOWN;
			for(int action:actions) { e.key.keysym.sym=SDLK_TAB; main.dispatchEvents(&e); e.key.keysym.sym=SDLK_RETURN; main.dispatchEvents(&e); require(main.result()==action,"main keyboard route"); }
			for(int action:actions) { main.clickButton(action); require(main.result()==action,"main mouse route"); }
		}
		{
			FrontendScope scope;
			GAGGUI::ScreenStack screens(*globalContainer->gfx);
			LoadSaveScreen dialog("replays","replay",false,"Save replay","",replayFilenameToName,glob2NameToFilename);
			dialog.dispatchPaint();
			SDL_Event text{}; text.type=SDL_TEXTINPUT; SDL_strlcpy(text.text.text,"colony-review",sizeof(text.text.text));
			dialog.dispatchEvents(&text);
			require(std::string(dialog.getName())=="colony-review","replay dialog text entry");
			SDL_Event key{}; key.type=SDL_KEYDOWN; key.key.keysym.sym=SDLK_BACKSPACE; dialog.dispatchEvents(&key);
			require(std::string(dialog.getName())=="colony-revie","replay dialog editing");
			key.key.keysym.sym=SDLK_ESCAPE; dialog.dispatchEvents(&key);
			require(dialog.endValue==LoadSaveScreen::CANCEL,"replay dialog cancellation");
			Preview<CustomGameScreen> custom(screens); custom.selectFirstListItem(); custom.render();
			require(custom.getMapHeader().getNumberOfTeams()>0,"map selection loads teams");
			key.key.keysym.sym=SDLK_ESCAPE; custom.dispatchEvents(&key);
			require(custom.result()==CustomGameScreen::CANCEL,"custom game cancellation");
		}
		std::cout << "PASS: presentation, bounded text, timing, isolation, determinism, scoped style, fallback\n";
		return 0;
	}
	if(mode=="sessions")
	{
		require(theme.colony->load(),"load session fixture");
		FrontendScope menu;
		for(bool replay : {false,true})
		{
			Engine engine;
			require((replay ? engine.loadReplay("replays/last_game.replay") : engine.initCampaign("maps/balanced.map"))==Engine::EE_NO_ERROR,"initialize real session");
			SessionExit sequence{0,globals.gfx->getW()/2,globals.gfx->getH()/2+(replay?25:50),replay};
			globals.replayFastForward=replay;
			const auto timer=SDL_AddTimer(500,exitSession,&sequence); require(timer,"session input timer");
			const int result=engine.run(); SDL_RemoveTimer(timer);
			require(result==Engine::EE_NO_ERROR,"return through results screen");
			require(GAGGUI::Style::style==&theme && FrontendTheme::allowed,"game and results restore menu theme");
		}
		globals.replaying=false; globals.replayFastForward=false;
		{
			auto editor=std::make_unique<MapEdit>();
			require(editor->load("maps/balanced.map"),"load editor fixture");
			GAGGUI::ScreenStack screens(*globals.gfx);
			screens.push(std::make_unique<MapEditorScreen>(screens,std::move(editor)));
			SessionExit sequence{0,globals.gfx->getW()/2,globals.gfx->getH()/2+75};
			const auto timer=SDL_AddTimer(500,exitSession,&sequence); require(timer,"editor input timer");
			const int result=screens.execute(40); SDL_RemoveTimer(timer);
			require(result==0,"return from editor");
			require(GAGGUI::Style::style==&theme && FrontendTheme::allowed,"editor restores menu theme");
		}
		const auto tick=theme.colony->tick(); theme.colony->update(SDL_GetTicks64());
		require(theme.colony->tick()==tick,"session return has no catch-up burst");
		std::cout << "PASS: game, replay, results and editor return to menu theme\n";
		return 0;
	}
	if(mode=="display")
	{
		require(theme.colony->load(),"load display fixture");
		const auto checksum=theme.colony->checksum();
		const auto rng=getSyncRandState();
		FrontendScope scope;
		for(const auto& size : {std::pair<int,int>{640,480},{1920,1080},{1152,720}})
		{
			require(globals.gfx->setRes(size.first,size.second,globals.settings.screenFlags),"change logical resolution");
			{ Preview<MainMenuScreen> screen; screen.render(); screen.checkBounds(); }
			globals.gfx->nextFrame();
		}
		require(theme.colony->checksum()==checksum && getSyncRandState()==rng,"display changes preserve simulation state");
		theme.colony=std::make_unique<MenuColony>();
		for(const auto& size : {std::pair<int,int>{640,480},{1920,1080}})
		{
			require(globals.gfx->setRes(size.first,size.second,globals.settings.screenFlags),"resize fallback");
			{ Preview<MainMenuScreen> screen; screen.render(); screen.checkBounds(); }
			globals.gfx->nextFrame();
		}
		std::cout << "PASS: live and fallback resolution changes preserve simulation state\n";
		return 0;
	}
	if(mode=="navigation")
	{
		GAGGUI::ScreenStack screens(*globals.gfx);
		{ Preview<MainMenuScreen> s; s.executeCancellation(); }
		{ Preview<CampaignMainMenu> s(screens); s.executeCancellation(); }
		{ Preview<CampaignSelectorScreen> s; s.executeCancellation(); }
		{ Preview<CampaignMenuScreen> s("campaigns/Tutorial_Campaign.txt",screens); s.executeCancellation(); }
		{ Preview<CustomGameScreen> s(screens); s.selectFirstListItem(); s.executeKeyboardCancellation(); }
		{ Preview<ChooseMapScreen> s("games","game",true); s.executeCancellation(); }
		{ Preview<SettingsScreen> s; s.executeEscape(); }
		{ Preview<EditorMainMenu> s(screens); s.executeCancellation(); }
		{ Preview<NewMapScreen> s; s.executeCancellation(); }
		{ Preview<LANMenuScreen> s(screens); s.executeCancellation(); }
		{ Preview<LANFindScreen> s(screens); s.executeCancellation(); }
		{ Preview<YOGLoginScreen> s(screens,std::make_shared<YOGClient>()); s.executeCancellation(); }
		{ Preview<YOGRegisterScreen> s(std::make_shared<YOGClient>()); s.executeCancellation(); }
		{ Preview<CreditScreen> s; s.executeCancellation(); }
		std::cout << "PASS: actual screen loops, mouse/keyboard exits, theme restoration\n";
		return 0;
	}
	if(mode=="record")
	{
		require(argc>=4,"record needs directory and frame count");
		require(theme.colony->load(),"load colony");
		std::filesystem::create_directories(argv[2]);
		FrontendScope scope;
		Preview<MainMenuScreen> menu;
		for(int i=0;i<std::atoi(argv[3]);++i)
		{
			theme.colony->update(1000+i*40);
			menu.render();
			DrawableSurface shot(globals.gfx->getW(),globals.gfx->getH());
			shot.drawSurface(0,0,globals.gfx);
			char name[32]; std::snprintf(name,sizeof(name),"/%04d.png",i);
			require(IMG_SavePNG(shot.getSDLSurface(),(std::string(argv[2])+name).c_str())==0,"record PNG");
			globals.gfx->nextFrame();
		}
		return 0;
	}
	if(mode=="soak")
	{
		require(theme.colony->load(),"load colony");
		const Uint64 start=SDL_GetTicks64(), end=start+std::atoi(argv[2])*1000ULL;
		Uint64 frames=0, cost=0, worst=0;
		double updateCost=0, worstUpdate=0, worstInput=0;
		FrontendScope scope;
		Preview<MainMenuScreen> menu;
		while(SDL_GetTicks64()<end)
		{
			const auto begin=SDL_GetTicks64();
			const auto updateStart=std::chrono::steady_clock::now();
			theme.colony->update(begin);
			const double updateMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-updateStart).count();
			updateCost+=updateMs; worstUpdate=std::max(worstUpdate,updateMs);
			menu.render(); globals.gfx->nextFrame();
			SDL_Event event; while(SDL_PollEvent(&event)) {}
			if(frames%125==0)
			{
				const auto inputStart=std::chrono::steady_clock::now();
				menu.clickButton(MainMenuScreen::CUSTOM);
				require(menu.result()==MainMenuScreen::CUSTOM,"soak input remains responsive");
				worstInput=std::max(worstInput,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-inputStart).count());
			}
			const auto elapsed=SDL_GetTicks64()-begin;
			cost+=elapsed; worst=std::max(worst,elapsed); ++frames;
			if(frames%1500==0) std::cout << "elapsed_ms=" << begin-start << " tick=" << theme.colony->tick() << " mean_frame_ms=" << double(cost)/frames << " max_frame_ms=" << worst << std::endl;
			if(elapsed<40) SDL_Delay(40-elapsed);
		}
		std::cout << "update_mean_ms=" << updateCost/frames << " update_max_ms=" << worstUpdate << " input_dispatch_max_ms=" << worstInput << std::endl;
		std::cout << "SOAK PASS frames=" << frames << " tick=" << theme.colony->tick() << " mean_frame_ms=" << double(cost)/frames << " max_frame_ms=" << worst << std::endl;
		return 0;
	}
	require(false,"unknown mode");
}
