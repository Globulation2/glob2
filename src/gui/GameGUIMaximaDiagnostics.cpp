#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "AI.h"
#include "AIImplementation.h"
#include "GlobalContainer.h"
#include "Player.h"
#include "Team.h"
#include "Unit.h"
#include "GraphicContext.h"
#include "GUIBase.h"
#include "FormatableString.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>
Player* GameGUI::findSameTeamMaximaPlayer(void) const
{
	if(localTeam==NULL)
		return NULL;
	for(int player=0; player<game.gameHeader.getNumberOfPlayers(); ++player)
	{
		Player* candidate=game.players[player];
		if(candidate && candidate->teamNumber==localTeamNo && candidate->ai
		   && candidate->ai->implementationID==AI::MAXIMA
		   && candidate->ai->aiImplementation)
			return candidate;
	}
	return NULL;
}


void GameGUI::drawMaximaDiagnostics(void)
{
	if(!showMaximaDiagnostics || globalContainer->replaying || localTeam==NULL)
		return;

	Player* maximaPlayer=findSameTeamMaximaPlayer();
	if(maximaPlayer==NULL)
		return;

	std::vector<AIDiagnosticSection> sections;
	maximaPlayer->ai->aiImplementation->getDiagnosticSections(sections);
	if(sections.empty())
		return;

	const int x=6;
	const int width=globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-x-4;
	int columnCount=2;
	if(width>=1200)
		columnCount=5;
	else if(width>=900)
		columnCount=4;
	else if(width>=600)
		columnCount=3;
	const int columnWidth=width/columnCount;
	Font* rowFont=columnWidth>=240
		? globalContainer->standardFont : globalContainer->littleFont;
	const int rowHeight=rowFont->getStringHeight("Ag")+2;
	const int sectionGap=4;
	const int headingHeight=globalContainer->standardFont->getStringHeight("Ag")+9;

	std::vector<std::vector<const AIDiagnosticSection*> > columns(columnCount);
	std::vector<int> columnHeights(columnCount, 0);
	for(size_t sectionIndex=0; sectionIndex<sections.size(); ++sectionIndex)
	{
		int shortest=0;
		for(int column=1; column<columnCount; ++column)
			if(columnHeights[column]<columnHeights[shortest])
				shortest=column;
		columns[shortest].push_back(&sections[sectionIndex]);
		columnHeights[shortest]+=
			(static_cast<int>(sections[sectionIndex].rows.size())+1)*rowHeight
			+sectionGap;
	}

	int contentHeight=0;
	for(int column=0; column<columnCount; ++column)
		contentHeight=std::max(contentHeight, columnHeights[column]);
	const int boxHeight=headingHeight+contentHeight+4;
	const int bottom=globalContainer->gfx->getH()-4;
	const int y=std::max(18, bottom-boxHeight);
	globalContainer->gfx->drawFilledRect(x, y, width, bottom-y,
		2, 5, 12, 225);
	globalContainer->gfx->drawFilledRect(x, y, width, headingHeight,
		13, 23, 38, 245);
	globalContainer->gfx->drawRect(x, y, width, bottom-y,
		145, 170, 195, 230);

	std::ostringstream heading;
	heading<<"MAXIMA STRATEGIC VIEW  |  "<<maximaPlayer->name
		<<"  |  F10: hide";
	globalContainer->standardFont->pushStyle(
		Font::Style(Font::STYLE_BOLD, maximaPlayer->team->color));
	globalContainer->gfx->drawString(x+8, y+4,
		globalContainer->standardFont, heading.str().c_str());
	globalContainer->standardFont->popStyle();

	for(int column=0; column<columnCount; ++column)
	{
		const int columnX=x+column*columnWidth;
		if(column>0)
			globalContainer->gfx->drawVertLine(columnX, y+headingHeight,
				bottom-y-headingHeight, 70, 88, 108, 190);
		int rowY=y+headingHeight+2;
		for(size_t sectionIndex=0; sectionIndex<columns[column].size();
			++sectionIndex)
		{
			const AIDiagnosticSection& section=*columns[column][sectionIndex];
			globalContainer->gfx->drawFilledRect(columnX+1, rowY,
				columnWidth-2, rowHeight, 20, 39, 58, 230);
			rowFont->pushStyle(Font::Style(Font::STYLE_BOLD, 245, 192, 86));
			globalContainer->gfx->drawString(columnX+7, rowY+1,
				rowFont, section.title.c_str());
			rowFont->popStyle();
			rowY+=rowHeight;

			for(size_t row=0; row<section.rows.size(); ++row)
			{
				const AIDiagnosticRow& entry=section.rows[row];
				if(row&1)
					globalContainer->gfx->drawFilledRect(columnX+1, rowY,
						columnWidth-2, rowHeight, 255, 255, 255, 9);
				rowFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 190, 202, 214));
				globalContainer->gfx->drawString(columnX+7, rowY+1,
					rowFont, entry.label.c_str());
				rowFont->popStyle();
				const int valueWidth=rowFont->getStringWidth(entry.value.c_str());
				rowFont->pushStyle(Font::Style(Font::STYLE_BOLD, 151, 220, 255));
				globalContainer->gfx->drawString(
					columnX+columnWidth-valueWidth-7, rowY+1,
					rowFont, entry.value.c_str());
				rowFont->popStyle();
				rowY+=rowHeight;
			}
			rowY+=sectionGap;
		}
	}
}


void GameGUI::drawMaximaTopologyDiagnostics(void)
{
	if(!showMaximaTopologyDiagnostics || globalContainer->replaying
	   || localTeam==NULL)
		return;
	Player* maximaPlayer=findSameTeamMaximaPlayer();
	if(maximaPlayer==NULL)
		return;
	const AITopologyDiagnosticSnapshot* snapshot=
		maximaPlayer->ai->aiImplementation->getTopologyDiagnosticSnapshot();
	if(snapshot==NULL || snapshot->width!=game.map.getW()
	   || snapshot->height!=game.map.getH() || snapshot->modes.empty())
		return;

	int pageCount=0;
	for(size_t mode=0; mode<snapshot->modes.size(); ++mode)
		pageCount+=1+snapshot->modes[mode].teams.size();
	if(pageCount<=0)
		return;
	maximaTopologyDiagnosticPage%=pageCount;
	if(maximaTopologyDiagnosticPage<0)
		maximaTopologyDiagnosticPage+=pageCount;
	int page=maximaTopologyDiagnosticPage;
	const AITopologyDiagnosticMode* selectedMode=NULL;
	const AITopologyDiagnosticTeam* selectedTeam=NULL;
	for(size_t mode=0; mode<snapshot->modes.size(); ++mode)
	{
		const AITopologyDiagnosticMode& candidate=snapshot->modes[mode];
		if(page==0)
		{
			selectedMode=&candidate;
			break;
		}
		--page;
		if(page<int(candidate.teams.size()))
		{
			selectedMode=&candidate;
			selectedTeam=&candidate.teams[page];
			break;
		}
		page-=candidate.teams.size();
	}
	if(selectedMode==NULL)
		return;

	const int mapPixelWidth=globalContainer->gfx->getW()-RIGHT_MENU_WIDTH;
	const int mapPixelHeight=globalContainer->gfx->getH();
	const int size=snapshot->width*snapshot->height;
	for(int index=0; index<size; ++index)
	{
		int px, py;
		game.map.mapCaseToDisplayable(index%snapshot->width,
			index/snapshot->width, &px, &py, viewportX, viewportY);
		if(px+32<=0 || py+32<=16 || px>=mapPixelWidth || py>=mapPixelHeight)
			continue;
		if(index<int(selectedMode->walkable.size())
		   && selectedMode->walkable[index])
			globalContainer->gfx->drawFilledRect(px, py, 32, 32,
				150, 180, 170, 55);
		else
			globalContainer->gfx->drawFilledRect(px, py, 32, 32,
				22, 24, 29, 125);
		const int home=index<int(selectedMode->homeDistance.size())
			? selectedMode->homeDistance[index] : -1;
		if(home>=snapshot->innerDistance && home<=snapshot->bandMaximum)
			globalContainer->gfx->drawFilledRect(px, py, 32, 32,
				40, 105, 210, 100);

		if(selectedTeam!=NULL)
		{
			if(index<int(selectedTeam->corridor.size())
			   && selectedTeam->corridor[index])
			{
				globalContainer->gfx->drawFilledRect(px, py, 32, 32,
					245, 190, 38, 165);
				const int corridorWidth=selectedTeam->corridorWidth[index];
				const int terrainWidth=selectedTeam->terrainWidth[index];
				if(corridorWidth>=0 && terrainWidth>=0
				   && corridorWidth<=snapshot->maximumCrossSection
				   && terrainWidth<=snapshot->maximumCrossSection)
				{
					globalContainer->gfx->drawFilledRect(px+3, py+3, 26, 26,
						240, 65, 220, 90);
					globalContainer->gfx->drawRect(px+2, py+2, 27, 27,
						240, 65, 220, 255);
				}
			}
		}
		else
		{
			const int memberships=index<int(selectedMode->memberships.size())
				? selectedMode->memberships[index] : 0;
			if(memberships>0)
				globalContainer->gfx->drawFilledRect(px, py, 32, 32,
					245, 132, 32, std::min(220, 80+memberships*40));
			if(index<int(selectedMode->qualified.size())
			   && selectedMode->qualified[index])
			{
				globalContainer->gfx->drawFilledRect(px+3, py+3, 26, 26,
					240, 65, 220, 90);
				globalContainer->gfx->drawRect(px+2, py+2, 27, 27,
					240, 65, 220, 255);
			}
		}
		if(index<int(snapshot->desired.size()) && snapshot->desired[index])
			globalContainer->gfx->drawFilledRect(px+5, py+5, 22, 22,
				20, 225, 235, 160);
	}

	for(size_t index=0; index<snapshot->candidates.size(); ++index)
	{
		const AITopologyDiagnosticCandidate& candidate=snapshot->candidates[index];
		if(candidate.mode!=selectedMode->mode || candidate.index<0)
			continue;
		int px, py;
		game.map.mapCaseToDisplayable(candidate.index%snapshot->width,
			candidate.index/snapshot->width, &px, &py, viewportX, viewportY);
		if(px+32<=0 || py+32<=16 || px>=mapPixelWidth || py>=mapPixelHeight)
			continue;
		if(candidate.state==AITopologyCandidateSelected)
		{
			globalContainer->gfx->drawRect(px+1, py+1, 29, 29,
				255, 255, 255, 255);
			globalContainer->gfx->drawLine(px+5, py+16, px+27, py+16,
				255, 255, 255, 255);
			globalContainer->gfx->drawLine(px+16, py+5, px+16, py+27,
				255, 255, 255, 255);
		}
		else if(candidate.state==AITopologyCandidateRejectedOverlap)
		{
			globalContainer->gfx->drawLine(px+6, py+6, px+26, py+26,
				255, 60, 60, 220);
			globalContainer->gfx->drawLine(px+26, py+6, px+6, py+26,
				255, 60, 60, 220);
		}
		else if(candidate.state==AITopologyCandidateRejectedCap)
			globalContainer->gfx->drawRect(px+7, py+7, 17, 17,
				145, 145, 145, 180);
	}

	std::ostringstream heading;
	heading<<"F9 / Ctrl+9 topology  |  "
		<<(selectedMode->mode==AITopologyLand ? "LAND" : "AMPHIBIOUS");
	if(selectedTeam)
		heading<<" team "<<selectedTeam->team;
	else
		heading<<" aggregate";
	heading<<"  |  tick "<<snapshot->tick<<"  |  candidates "
		<<selectedMode->candidateCount<<"  |  selected "
		<<selectedMode->selectedCount<<" ("<<snapshot->selectedCount
		<<"/"<<snapshot->effectiveZoneCap<<" total)"
		<<"  |  Shift+F9 / Ctrl+Shift+9: next";
	const int legendWidth=std::min(mapPixelWidth-12,
		globalContainer->littleFont->getStringWidth(heading.str().c_str())+16);
	globalContainer->gfx->drawFilledRect(6, 20, legendWidth, 18,
		3, 8, 14, 220);
	globalContainer->gfx->drawString(12, 22, globalContainer->littleFont,
		heading.str().c_str());

	struct TopologyLegendItem
	{
		const char* label;
		int red;
		int green;
		int blue;
	};
	static const TopologyLegendItem legendItems[]={
		{"Walkable", 150, 180, 170},
		{"Blocked / unknown", 22, 24, 29},
		{"Defensive band", 40, 105, 210},
		{"Route / membership", 245, 132, 32},
		{"Qualified choke", 240, 65, 220},
		{"Final guard zone", 20, 225, 235},
		{"Selected center", 255, 255, 255},
		{"Overlap rejection", 255, 60, 60},
		{"Capacity rejection", 145, 145, 145}
	};
	const int legendTop=40;
	const int legendHeight=54;
	const int legendColumns=3;
	const int legendCellWidth=(mapPixelWidth-24)/legendColumns;
	globalContainer->gfx->drawFilledRect(6, legendTop, mapPixelWidth-12,
		legendHeight, 3, 8, 14, 225);
	for(int legendIndex=0; legendIndex<9; ++legendIndex)
	{
		const int column=legendIndex%legendColumns;
		const int row=legendIndex/legendColumns;
		const int itemX=12+column*legendCellWidth;
		const int itemY=legendTop+3+row*17;
		const TopologyLegendItem& item=legendItems[legendIndex];
		globalContainer->gfx->drawFilledRect(itemX, itemY+1, 13, 13,
			item.red, item.green, item.blue, 220);
		globalContainer->gfx->drawRect(itemX, itemY+1, 13, 13,
			235, 240, 245, 230);
		globalContainer->gfx->drawString(itemX+18, itemY,
			globalContainer->littleFont, item.label);
	}

	if(mouseX>=0 && mouseX<mapPixelWidth && mouseY>=16
	   && mouseY<mapPixelHeight)
	{
		int mapX, mapY;
		game.map.displayToMapCaseAligned(mouseX, mouseY, &mapX, &mapY,
			viewportX, viewportY);
		const int index=mapY*snapshot->width+mapX;
		std::ostringstream detail;
		detail<<mapX<<","<<mapY<<"  H="<<selectedMode->homeDistance[index];
		if(selectedTeam)
		{
			const int enemy=selectedTeam->enemyDistance[index];
			detail<<"  E="<<enemy;
			if(selectedMode->homeDistance[index]>=0 && enemy>=0
			   && selectedTeam->shortestDistance>=0)
				detail<<"  excess="
					<<selectedMode->homeDistance[index]+enemy
						-selectedTeam->shortestDistance
					<<"/"<<snapshot->pathSlack;
			detail<<"  width="<<selectedTeam->corridorWidth[index]
				<<"/"<<selectedTeam->terrainWidth[index];
		}
		else
			detail<<"  memberships="<<selectedMode->memberships[index]
				<<"  width="<<selectedMode->minimumCrossSection[index]
				<<"/"<<selectedMode->minimumTerrainCrossSection[index];
		for(size_t candidateIndex=0;
			candidateIndex<snapshot->candidates.size(); ++candidateIndex)
			if(snapshot->candidates[candidateIndex].mode==selectedMode->mode
			   && snapshot->candidates[candidateIndex].index==index)
			{
				static const char* states[]={
					"unselected", "selected", "overlap", "cap"
				};
				const int state=int(snapshot->candidates[candidateIndex].state);
				detail<<"  candidate="
					<<(state>=0 && state<4 ? states[state] : "unknown");
			}
		const int detailWidth=std::min(mapPixelWidth-12,
			globalContainer->littleFont->getStringWidth(detail.str().c_str())+16);
		globalContainer->gfx->drawFilledRect(6, 96, detailWidth, 18,
			3, 8, 14, 220);
		globalContainer->gfx->drawString(12, 98, globalContainer->littleFont,
			detail.str().c_str());
	}
}

