// SPDX-License-Identifier: GPL-3.0-or-later
// Link the production game renderer; expose internals only in this test TU.
#undef NDEBUG
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
#include "GlobalContainer.h"
#ifdef HAVE_OPENGL
#include <SDL_opengl.h>
#endif
#include "Engine.h"
#include "Unit.h"
#include "Bullet.h"
#include "render/GameAnimations.h"
#include "SettingsScreen.h"
#include "FrontendTheme.h"
#include "MapEdit.h"
#include "gui/GameGUIViewport.h"
#include <GUIList.h>
#include <GUIText.h>
#include "CreditScreen.cpp"
#include <cassert>
#include <iostream>

GlobalContainer *globalContainer = nullptr;

class Credits : public ScrollingText
{
public:
	Credits() : ScrollingText(0,0,0,0,ALIGN_FILL,ALIGN_FILL,"standard","data/authors.txt")
		{ text = {"Resize regression"}; }
	void resetOffset() { offset = 0; }
};
class ScreenProbe : public Screen
{
public:
	void attach(DrawableSurface *surface) { gfx = surface; }
	void onAction(Widget*, Action, int, int) override {}
};

static int colored(SDL_Surface *surface, int x, int y, int w, int h)
{
	int count = 0;
	for (int yy=std::max(0,y); yy<std::min(surface->h,y+h); ++yy)
		for (int xx=std::max(0,x); xx<std::min(surface->w,x+w); ++xx)
		{
			Uint32 pixel;
			memcpy(&pixel, static_cast<char*>(surface->pixels)+yy*surface->pitch+xx*4, 4);
			Uint8 r,g,b;
			SDL_GetRGB(pixel,surface->format,&r,&g,&b);
			count += r || g || b;
		}
	return count;
}

static void capturePixels(GraphicContext *gfx)
{
#ifdef HAVE_OPENGL
	if (gfx->getOptionFlags() & GraphicContext::USEGPU)
	{
		SDL_Surface *surface=gfx->getSDLSurface();
		std::vector<Uint8> pixels(surface->w*surface->h*4);
		glReadBuffer(GL_BACK);
		glReadPixels(0,0,surface->w,surface->h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
		assert(glGetError()==GL_NO_ERROR);
		for(int y=0;y<surface->h;++y) for(int x=0;x<surface->w;++x)
		{
			const auto *p=&pixels[((surface->h-1-y)*surface->w+x)*4];
			const Uint32 value=SDL_MapRGBA(surface->format,p[0],p[1],p[2],p[3]);
			memcpy(static_cast<char*>(surface->pixels)+y*surface->pitch+x*4,&value,4);
		}
	}
#endif
}

int main(int argc, char **argv)
{
	std::cout << std::unitbuf;
	SDL_SetMainReady();
	assert(argc == 2 || (argc == 3 && std::string(argv[2]) == "--gl"));
	const bool gpu=argc==3;
	GlobalContainer globals(argv[1]);
	globalContainer = &globals;
	globals.settings.screenWidth=1800;
	globals.settings.screenHeight=1100;
	globals.settings.screenFlags=GraphicContext::RESIZABLE | (gpu ? GraphicContext::USEGPU : 0);
	globals.settings.mute=true;
	globals.load();
	globals.settings.rememberUnit=false;
	GameGUI gui;
	Game &game=gui.game;
	game.map.setSize(4,4,GRASS);
	game.map.setGame(&game);
	game.addTeam(0);
	game.teams[0]->race.loadDefault();
	gui.localTeamNo=0;
	gui.localTeam=game.teams[0];
	gui.teamStats=&game.teams[0]->stats;
	gui.viewportX=gui.viewportY=0;
	for(int y=0;y<16;++y) for(int x=0;x<16;++x) game.map.clearImmobileUnit(x,y);
	auto *unit=game.addUnit(3,3,0,WORKER,0,255,0,0);
	auto *building=game.addBuilding(7,7,globals.buildingsTypes.getTypeNum("inn",0,false),0);
	assert(unit && building);
	auto *gfx=globals.gfx;
	// The resize cases below need the desktop to actually fit an 1800x1100
	// window; a shorter/narrower usable area (small display, no Xvfb) makes
	// SDL clamp the requested size instead of honoring it. Checked for both
	// backends: only the GPU path additionally needs the drawable unscaled.
	{
		SDL_Rect usable{};
		const int displayIndex = SDL_GetWindowDisplayIndex(gfx->window);
		if (SDL_GetDisplayUsableBounds(displayIndex, &usable) == 0 && (usable.w < 1800 || usable.h < 1100))
		{
			std::cerr << "Rendering fixture requires an 1800x1100 desktop; usable area is "
				<< usable.w << "x" << usable.h << ". Use a sufficiently large desktop or Xvfb.\n";
			return 0;
		}
	}
	if (gpu)
	{
		int drawableW,drawableH;
		SDL_GL_GetDrawableSize(gfx->window,&drawableW,&drawableH);
		if (drawableW!=gfx->getW() || drawableH!=gfx->getH())
		{
			std::cerr << "Rendering fixture requires an unscaled 1800x1100 drawable; got "
				<< drawableW << "x" << drawableH << ". Use a sufficiently large desktop or Xvfb.\n";
			return 0;
		}
	}
	const auto resize = [&](int width, Screen *screen = nullptr) {
		SDL_SetWindowSize(gfx->window,width,1100);
		SDL_Delay(60);
		SDL_Event event;
		while (GraphicContext::pollEvent(&event))
			if (screen) screen->dispatchEvents(&event);
		gfx->updateWindowSize();
		assert(gfx->getW()==width && gfx->getH()==1100);
		// Keep a fixed test origin when changing size; camera centering itself
		// is covered by the high-resolution integration harness.
		gui.camera.width=gui.camera.height=0;
		gui.camera.originX=gui.camera.originY=0;
	};
	const auto clear = [&] { gfx->setClipRect(); gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0); };
	// Six displayed copies: three across and two down. Compare exact pixel counts,
	// not just nonblack output somewhere in the viewport.
	const auto copies = [&](int x, int y, int w, int h) {
		capturePixels(gfx);
		int expected=colored(gfx->getSDLSurface(),x,y,w,h);
		assert(expected > 0);
		SDL_Surface *surface=gfx->getSDLSurface();
		for(int row=0;row<2;++row) for(int col=0;col<3;++col)
		{
			assert(colored(surface,x+col*512,y+row*512,w,h)==expected);
			int totalDifference=0;
			for(int yy=0;yy<h;++yy) for(int xx=0;xx<w;++xx)
			{
				Uint32 first,copy;
				memcpy(&first,static_cast<char*>(surface->pixels)+(y+yy)*surface->pitch+(x+xx)*4,4);
				memcpy(&copy,static_cast<char*>(surface->pixels)+(y+yy+row*512)*surface->pitch+(x+xx+col*512)*4,4);
				Uint8 r,g,b,cr,cg,cb;
				SDL_GetRGB(first,surface->format,&r,&g,&b);
				SDL_GetRGB(copy,surface->format,&cr,&cg,&cb);
				assert((r || g || b) == (cr || cg || cb));
				totalDifference += std::abs(r-cr)+std::abs(g-cg)+std::abs(b-cb);
			}
			// Require identical geometry. GL line antialiasing can change a
			// single edge pixel substantially when translated, so bound mean
			// RGB error over colored pixels (never dilute it with black padding).
			assert(totalDifference <= (gpu ? expected*3 : 0));
		}
	};
	Game::ViewState view;
	clear();
	game.drawMapGroundUnits(0,0,52,34,1600,1100,0,0,0,Game::DRAW_WHOLE_MAP,view);
	copies(96,96,32,32);
	clear();
	game.drawMapGroundBuildings(0,0,52,34,1600,1100,0,0,0,Game::DRAW_WHOLE_MAP,nullptr,nullptr);
	copies(224,224,96,96);
	std::cout << "PASS repeated unit/building sprites\n";

	gui.setSelection(GameGUI::BUILDING_SELECTION,building);
	gui.showUnitWorkingToBuilding=true;
	building->unitsWorking.push_back(unit);
	clear(); gui.drawOverlayInfos();
	copies(200,200,160,160);
	copies(94,94,36,36);
	// GUI overlay must not paint into the right-hand menu.
	assert(colored(gfx->getSDLSurface(),gfx->getW()-128,80,128,600)==0);
	building->unitsWorking.clear();
	gui.setSelection(GameGUI::RESOURCE_SELECTION,static_cast<unsigned>(3+3*16));
	clear(); gui.drawOverlayInfos(); copies(94,94,36,36);
	gui.clearSelection();
	std::cout << "PASS building, worker and resource selection copies\n";

	unit->validTarget=true;
	unit->targetX=5; unit->targetY=3;
	clear();
	game.drawUnitPathLine(0,0,52,34,1600,1100,0,0,0,0,unit);
	copies(110,110,70,5);
	// A line crossing the map seam follows the short route in every copy.
	unit->posX=15; unit->targetX=1;
	clear();
	game.drawUnitPathLine(0,0,52,34,1600,1100,0,0,0,0,unit);
	capturePixels(gfx);
	assert(colored(gfx->getSDLSurface(),0,110,48,5)>0);
	assert(colored(gfx->getSDLSurface(),100,110,350,5)==0);
	copies(495,110,60,5);
	unit->posX=3;
	std::cout << "PASS repeated path lines and short seam crossings\n";

	game.animations->resize(game.map.getSectorW()*game.map.getSectorH());
	game.map.setMapDiscovered(3,3,game.teams[0]->me);
	game.map.fogOfWar[game.map.coordToIndex(3,3)] = game.teams[0]->me;
	game.animations->onBulletImpact(game.map,3,3);
	game.animations->step();
	clear(); game.drawMapBulletsExplosionsDeathAnimations(0,0,52,34,1600,1100,0,0,0,0);
	copies(80,80,64,64);
	game.animations->clear();
	game.animations->onUnitDeath(game.map,3,3,game.teams[0]);
	clear(); game.drawMapBulletsExplosionsDeathAnimations(0,0,52,34,1600,1100,0,0,0,0);
	copies(70,40,90,120);
	game.animations->clear();
	auto *bullet=new Bullet(96,96,0,0,10,1,3,3,3,3,1,1);
	game.map.getSector(0)->bullets.push_back(bullet);
	clear(); game.drawMapBulletsExplosionsDeathAnimations(0,0,52,34,1600,1100,0,0,0,0);
	copies(90,90,48,48);
	game.map.getSector(0)->bullets.clear(); delete bullet;
	std::cout << "PASS repeated bullets, explosions and deaths\n";

	clear();
	gui.ghostManager.addBuilding(building->typeNum,7,7);
	gui.ghostManager.drawAll(0,0,0);
	copies(224,224,96,96);
	gui.ghostManager.removeBuilding(7,7);
	clear();
	Mark marker(3,3,Color(255,255,255),60);
	marker.showTicks=45;
	marker.drawInMainView(0,0,game);
	copies(20,20,160,160);
	int clipX,clipY,clipW,clipH;
	gfx->getClipRect(&clipX,&clipY,&clipW,&clipH);
	assert(clipX==0 && clipY==0 && clipW==gfx->getW() && clipH==gfx->getH());
	clear();
	auto *particle=new GameGUI::Particle{};
	particle->x=112; particle->y=112; particle->lifeSpan=50;
	particle->startImg=0; particle->endImg=2; particle->color=Color(255,255,255);
	gui.particles.insert(particle);
	gui.drawParticles(true);
	copies(80,80,64,64);
	assert(particle->age==1); // One update despite six displayed copies.
	particle->vx=1; particle->vy=-2;
	for (int frame=0; frame<100; ++frame)
		gui.drawParticles(false);
	assert(particle->age==1 && particle->x==112 && particle->y==112);
	gui.drawParticles(true);
	assert(particle->age==2 && particle->x==113 && particle->y==110);
	std::set<Building*> smokeBuildings{building};
	building->hp=1;
	game.stepCounter=4; // An emission tick remains fixed during pause.
	gui.gamePaused=true;
	for (int frame=0; frame<100; ++frame)
		gui.generateNewParticles(&smokeBuildings);
	assert(gui.particles.size()==1);
	gui.gamePaused=false;
	gui.generateNewParticles(&smokeBuildings);
	assert(gui.particles.size()==2);
	for (auto *p : gui.particles) delete p;
	gui.particles.clear();
	building->hp=building->type->hpMax;
	std::cout << "PASS pause freezes particle emission and physics; resume restarts both\n";
	std::cout << "PASS ghosts, map markers and particles; one particle update\n";

	if (gpu)
	{
		gui.setSelection(GameGUI::RESOURCE_SELECTION,static_cast<unsigned>(3+3*16));
		for (double zoom : {0.5,2.0})
		{
			gui.camera=MapCamera{}; gui.camera.zoom=zoom;
			gui.viewportX=gui.viewportY=0;
			clear(); gui.drawOverlayInfos(); capturePixels(gfx);
			const int start=int(96*zoom), diameter=int(32*zoom), period=int(512*zoom);
			const int expected=colored(gfx->getSDLSurface(),start,start,diameter+1,diameter+1);
			assert(expected>0);
			for (int y=start;y+diameter<1100;y+=period)
				for (int x=start;x+diameter<gfx->getW()-GAME_GUI_RIGHT_MENU_WIDTH-32;x+=period)
					assert(colored(gfx->getSDLSurface(),x,y,diameter+1,diameter+1)==expected);
		}
		gui.camera=MapCamera{}; gui.viewportX=gui.viewportY=0;
		gui.clearSelection();
		std::cout << "PASS repeated selections at half and double map zoom\n";
	}

	// A complete production map frame catches the separate virtual-flag pass.
	unit->validTarget=false;
	globals.settings.optionFlags |= GlobalContainer::OPTION_LOW_SPEED_GFX;
	clear();
	game.drawMap(0,0,1800,1100,160,0,0,0,0,view,Game::DRAW_WHOLE_MAP);
	capturePixels(gfx);
	SDL_Surface *baseline=SDL_ConvertSurface(gfx->getSDLSurface(),gfx->getSDLSurface()->format,0);
	assert(baseline);
	auto *flag=game.addBuilding(3,3,globals.buildingsTypes.getTypeNum("warflag",0,false),0);
	assert(flag); flag->unitStayRange=1; view.selectedBuilding=flag;
	clear();
	game.drawMap(0,0,1800,1100,160,0,0,0,0,view,Game::DRAW_WHOLE_MAP);
	capturePixels(gfx);
	for(int row=0;row<2;++row) for(int col=0;col<3;++col)
	{
		int different=0;
		for(int y=60+row*512;y<170+row*512;++y) for(int x=60+col*512;x<170+col*512;++x)
			different += memcmp(static_cast<char*>(baseline->pixels)+y*baseline->pitch+x*4,
				static_cast<char*>(gfx->getSDLSurface()->pixels)+y*gfx->getSDLSurface()->pitch+x*4,4)!=0;
		assert(different>100);
	}
	SDL_FreeSurface(baseline);
	std::cout << "PASS virtual flags and ranges in complete map frames\n";

	// Exercise selected overlays after a seam-crossing pan and shrink/grow cycle.
	gui.setSelection(GameGUI::BUILDING_SELECTION,building);
	for(int width: {640,1200,1800})
	{
		resize(width);
		gui.viewportX=14; gui.viewportY=14;
		clear(); gui.drawOverlayInfos(); capturePixels(gfx);
		const int cx=((building->posX-14)&15)*32+building->type->width*16;
		const int cy=((building->posY-14)&15)*32+building->type->height*16;
		for(int y=cy;y+48<1100;y+=512) for(int x=cx;x+48<width-GAME_GUI_RIGHT_MENU_WIDTH;x+=512)
			assert(colored(gfx->getSDLSurface(),x-48,y-48,96,96)>0);
		assert(colored(gfx->getSDLSurface(),width-128,80,128,600)==0);
	}
	gui.clearSelection(); gui.viewportX=gui.viewportY=0;
	std::cout << "PASS overlay resize, panning and sidebar clipping\n";

	{
		MapEdit editor;
		editor.game.map.setSize(4,4,GRASS); editor.game.map.setGame(&editor.game);
		editor.game.addTeam(0); editor.game.teams[0]->race.loadDefault();
		editor.team=0; editor.viewportX=editor.viewportY=0;
		editor.mouseX=300; editor.mouseY=300;
		Building *selected=editor.game.addBuilding(7,7,globals.buildingsTypes.getTypeNum("inn",0,false),0);
		assert(selected);
		editor.selectionMode=MapEdit::PlaceNothing;
		clear(); editor.drawMap(0,0,1800,1100); capturePixels(gfx);
		SDL_Surface *before=SDL_ConvertSurface(gfx->getSDLSurface(),gfx->getSDLSurface()->format,0);
		assert(before);
		editor.selectionMode=MapEdit::EditingBuilding;
		editor.selectedBuildingGID=selected->gid;
		clear(); editor.drawMap(0,0,1800,1100); capturePixels(gfx);
		for(int row=0;row<2;++row) for(int col=0;col<3;++col)
		{
			int different=0;
			for(int y=200+row*512;y<330+row*512;++y) for(int x=200+col*512;x<330+col*512;++x)
				different += memcmp(static_cast<char*>(before->pixels)+y*before->pitch+x*4,
					static_cast<char*>(gfx->getSDLSurface()->pixels)+y*gfx->getSDLSurface()->pitch+x*4,4)!=0;
			assert(different>10);
		}
		SDL_FreeSurface(before);
	}
	std::cout << "PASS editor building selection in complete map frames\n";

	// Unequal horizontal/vertical periods catch accidental use of the width
	// mask or period for both axes. Cross both seams, in both directions.
	for (const auto dimensions : {std::pair<int,int>{4,5}, {5,4}})
	{
		GameGUI rectangular;
		auto &world=rectangular.game;
		world.map.setSize(dimensions.first,dimensions.second,GRASS);
		world.map.setGame(&world);
		world.addTeam(0); world.teams[0]->race.loadDefault();
		rectangular.localTeamNo=0; rectangular.localTeam=world.teams[0];
		rectangular.teamStats=&world.teams[0]->stats;
		const int mapW=world.map.getW(), mapH=world.map.getH();
		for(int y=0;y<mapH;++y) for(int x=0;x<mapW;++x) world.map.clearImmobileUnit(x,y);
		auto *walker=world.addUnit(1,1,0,WORKER,0,255,0,0);
		assert(walker); walker->validTarget=true;
		for(int width : {640,1200,1800}) for(int reverse : {0,1})
		{
			resize(width);
			walker->posX=reverse ? 1 : mapW-1;
			walker->posY=reverse ? 1 : mapH-1;
			walker->targetX=reverse ? mapW-1 : 1;
			walker->targetY=reverse ? mapH-1 : 1;
			clear();
			gfx->setClipRect(0,0,width-GAME_GUI_RIGHT_MENU_WIDTH,1100);
			world.drawUnitPathLine(0,0,width/32,34,width-GAME_GUI_RIGHT_MENU_WIDTH,1100,0,0,0,0,walker);
			capturePixels(gfx);
			assert(colored(gfx->getSDLSurface(),0,0,50,50)>0);
			assert(colored(gfx->getSDLSurface(),100,100,250,250)==0);
			// The short diagonal continues through each visible map corner.
			for(int y=0;y+50<1100;y+=mapH*32)
				for(int x=0;x+50<width-GAME_GUI_RIGHT_MENU_WIDTH;x+=mapW*32)
					assert(colored(gfx->getSDLSurface(),x,y,50,50)>0);
			assert(colored(gfx->getSDLSurface(),width-GAME_GUI_RIGHT_MENU_WIDTH,0,GAME_GUI_RIGHT_MENU_WIDTH,1100)==0);
			// Pan the far corner into view, below the top status bar.
			rectangular.camera=MapCamera{};
			rectangular.viewportX=mapW-3; rectangular.viewportY=mapH-3;
			rectangular.setSelection(GameGUI::RESOURCE_SELECTION,static_cast<unsigned>(mapW*mapH-1));
			clear(); rectangular.drawOverlayInfos(); capturePixels(gfx);
			const int selectionPixels=colored(gfx->getSDLSurface(),64,64,34,34);
			assert(selectionPixels>0);
			for(int y=64;y+34<1100;y+=mapH*32)
				for(int x=64;x+34<width-GAME_GUI_RIGHT_MENU_WIDTH-32;x+=mapW*32)
					assert(colored(gfx->getSDLSurface(),x,y,34,34)==selectionPixels);
			assert(colored(gfx->getSDLSurface(),width-128,80,128,600)==0);
		}
	}
	resize(1800);
	std::cout << "PASS rectangular maps, bidirectional corner crossings and narrow viewports\n";

	SettingsScreen settings;
	settings.gfx=gfx;
	settings.dispatchInit();
	for(int width : {640,1200,1800})
	{
		resize(width, &settings);
		assert(settings.actDisplay->getText()==settings.actDisplayModeToString());
	}
	std::cout << "PASS Settings display label follows native window resizing\n";
	assert(!settings.modeList->visible);
	settings.activateGroup(settings.keyboardGroup);
	settings.activateGroup(settings.generalGroup);
	assert(!settings.modeList->visible);
	// Exercise the same toggle callback without recreating the dummy window.
	globals.settings.screenFlags |= GraphicContext::USEGPU;
	settings.fullscreen->setState(true); settings.setFullscreen();
	assert(settings.modeList->visible);
	settings.activateGroup(settings.keyboardGroup);
	assert(!settings.modeList->visible);
	settings.activateGroup(settings.generalGroup);
	assert(settings.modeList->visible);
	int chosenW,chosenH;
	assert(sscanf(settings.modeList->getText(0).c_str(), "%dx%d", &chosenW, &chosenH)==2);
	settings.handleListSelected(settings.modeList,0);
	assert(globals.settings.screenWidth==chosenW && globals.settings.screenHeight==chosenH);
	assert(globals.settings.screenFlags & GraphicContext::FULLSCREEN);
	settings.fullscreen->setState(false); settings.setFullscreen();
	assert(!settings.modeList->visible);
	const int oldWidth=globals.settings.screenWidth;
	settings.handleListSelected(settings.modeList,0);
	assert(globals.settings.screenWidth==oldWidth);
	globals.settings.screenFlags=GraphicContext::RESIZABLE | (gpu ? GraphicContext::USEGPU : 0);
	std::cout << "PASS fullscreen-only resolution choices and tab switching\n";

	ScreenProbe screen;
	screen.attach(gfx);
	auto *credits=new Credits;
	screen.addWidget(credits); screen.dispatchInit(); credits->resetOffset();
	for(int width: {1800,640,1200})
	{
		resize(width);
		clear(); credits->paint(); capturePixels(gfx);
		int lo=width,hi=-1;
		for(int x=0;x<width;++x) if(colored(gfx->getSDLSurface(),x,0,1,40)) {lo=std::min(lo,x);hi=std::max(hi,x);}
		assert(hi>lo && std::abs((lo+hi)-width)<12);
	}
	std::cout << "PASS credits centered after resizing\n";
	{
		FrontendTheme theme;
		gfx->setClipRect();
		theme.background(gfx, false);
		gfx->nextFrame();
		gfx->presentLastFrame();
		// Menu fallback textures and cached expose presentation must not
		// affect the color/opacity of subsequent untextured gameplay drawing.
		gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),0,0,0);
		capturePixels(gfx);
		assert(colored(gfx->getSDLSurface(),0,0,gfx->getW(),gfx->getH())==0);
		gfx->drawFilledRect(0,0,gfx->getW(),gfx->getH(),255,255,255);
		std::valarray<unsigned char> opaque(static_cast<unsigned char>(255),4);
		gfx->drawAlphaMap(opaque,2,2,0,0,gfx->getW(),gfx->getH(),Color(0,0,0));
		capturePixels(gfx);
		assert(colored(gfx->getSDLSurface(),0,0,gfx->getW(),gfx->getH())==0);
	}
	std::cout << "PASS menu-to-game opaque drawing after cached presentation\n";
	return 0;
}
